# h264parse 調査資料

## 0. 調査対象

- 対象: GStreamer `gst-plugins-bad` の `videoparsersbad/h264parse`
- 参照 commit: `ca8068c6d793d7aaa6f2e2cc6324fdedfe2f33fa` (`master`, 2026-06-11 取得)
- 主な参照ファイル:
  - `gst/videoparsers/gsth264parse.c`
  - `gst/videoparsers/gsth264parse.h`
  - `tests/check/elements/h264parse.c`
  - 公式ドキュメント: `https://gstreamer.freedesktop.org/documentation/videoparsersbad/h264parse.html`

本資料は、`h264parse` に独自機能を追加する際の設計参考として、公開 API だけでなく内部状態、バッファ変換、イベント処理、テストで固定されている挙動を整理する。

## 1. h264parse の機能

`h264parse` は H.264 elementary stream を解析し、下流が扱いやすい `video/x-h264, parsed=true` のストリームへ整形する `GstBaseParse` 派生 element である。element metadata は `H.264 parser`、分類は `Codec/Parser/Converter/Video`、rank は `primary + 1` として登録される。

主な機能は次の通り。

- H.264 NAL unit を識別し、SPS/PPS/SEI/slice/AUD などの NAL type ごとに必要な解析を行う。
- `stream-format` を `byte-stream`, `avc`, `avc3` の間で変換する。
- `alignment` を `nal` または `au` に揃える。AU 出力時は複数 NAL を 1 access unit として集約し、NAL 出力時は NAL 単位で出す。
- SPS/PPS から width, height, framerate, PAR, profile, level, chroma-format, bit depth, colorimetry などを抽出し、src caps に反映する。
- AVC/AVC3 用の `codec_data` を生成または解釈する。
- `config-interval` に基づき SPS/PPS を IDR 付近に挿入する。
- `update-timecode=true` の場合、既存の Picture Timing SEI を upstream の `GstVideoTimeCodeMeta` で更新する。ただし SEI を新規挿入する機能ではない。
- Picture Timing SEI から `GstVideoTimeCodeMeta` を付与する。
- registered user data SEI から closed caption などの user data meta を抽出する。
- HDR 系 SEI の mastering display info / content light level を caps に反映する。
- stereo / multiview 系 SEI から `multiview-mode` / `multiview-flags` を caps に反映する。
- `GstForceKeyUnit` event を扱い、必要に応じて SPS/PPS を再送する。
- trickmode forward predicted segment では B slice を drop できる。

内部状態は `GstH264Parse` に集約される。重要なフィールドは、入力/出力 format と alignment、NAL parser、SPS/PPS cache、`codec_data`、timestamp 補間用 state、AU 検出用 state、SEI 解析結果、AUD 挿入 state、force-key-unit pending state である。

## 2. 使用用途

典型的な用途は、H.264 bitstream を decoder, muxer, RTP payloader, file sink などの要求に合わせて整形することである。

例:

```sh
filesrc location=input.h264 ! h264parse ! avdec_h264 ! videoconvert ! autovideosink
```

```sh
... ! x264enc ! h264parse config-interval=-1 ! rtph264pay ! ...
```

```sh
... ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! cpuh264dec ! ...
```

このリポジトリの `gst_codec_h264_caps_are_supported()` は `stream-format=byte-stream` かつ `alignment=au` のみを受けるため、現状の `cpuh264dec` 前段に置く parser として特に相性がよい。独自 decoder 側で NAL/AU 分割や AVC codec_data 解釈を抱え込まず、`h264parse` に境界検出と format 変換を任せられる。

## 3. 入出力仕様

### 3.1 pad template / caps

`sink` pad template:

```text
video/x-h264
```

`src` pad template:

```text
video/x-h264,
  parsed = true,
  stream-format = { avc, avc3, byte-stream },
  alignment = { au, nal }
```

実際の src caps には、SPS/SEI/upstream caps に応じて以下が追加または維持される。

- `width`, `height`
- `framerate`
- `pixel-aspect-ratio`
- `profile`, `level`
- `chroma-format`
- `bit-depth-luma`, `bit-depth-chroma`
- `colorimetry`
- `multiview-mode`, `multiview-flags`
- `mastering-display-info`
- `content-light-level`
- `codec_data` (`avc` / `avc3` かつ `alignment=au` の場合)

### 3.2 property

| property | type | default | 仕様 |
| --- | --- | --- | --- |
| `config-interval` | `gint`, `-1..3600` | `0` | SPS/PPS 挿入間隔。`0` は無効、`-1` は全 IDR frame に付与、正値は秒単位で間隔挿入。 |
| `update-timecode` | `gboolean` | `false` | upstream buffer の `GstVideoTimeCodeMeta` を使い、既存の Picture Timing SEI を更新する。SEI が存在しない場合は挿入しない。 |

### 3.3 sink 側入力

入力は `video/x-h264` で、caps から `stream-format`, `alignment`, `codec_data` を読み取る。

- `stream-format=avc`
  - `codec_data` が必須。
  - `alignment=au` のみ有効。`alignment=nal` は拒否される。
  - `codec_data` は avcC として解析され、NAL length size, SPS, PPS を取り出す。
- `stream-format=avc3`
  - `codec_data` は任意。SPS/PPS は in-band に存在しうる。
  - `codec_data` がある場合は avcC として解析され、packetized input として扱われる。
  - `codec_data` がない場合は拒否されないが、実装上は `/* probably AVC3 without codec_data field, anything to do here? */` という扱いで、明示的な avcC 解析は行われない。
- `stream-format=byte-stream`
  - Annex-B start code 付き NAL stream。
  - `codec_data` がある caps は拒否される。SPS/PPS は in-band または streamheader 側が前提。
- `stream-format` がない古い caps
  - `codec_data` があれば `avc`、なければ `byte-stream` と推定する。

### 3.4 src 側出力バッファ

出力は negotiation 後の `format/alignment` に従う。

- `byte-stream`: start code prefix 付き NAL を出力する。必要に応じて AUD を挿入する。
- `avc` / `avc3`: NAL length prefix 付き packetized 形式を出力する。
- `alignment=nal`: 1 buffer は原則 1 NAL。SPS/PPS 再送時は別 buffer として push される。
- `alignment=au`: 1 buffer は 1 access unit。SPS/PPS 再送時は IDR 位置の前に挿入される。

付与/更新される主な buffer flag/meta:

- `GST_BUFFER_FLAG_DELTA_UNIT`: keyframe でない場合に set、keyframe では unset。
- `GST_BUFFER_FLAG_HEADER`: SPS/PPS/SEI など header NAL を含む場合に set。
- `GST_BUFFER_FLAG_DISCONT`: 入力 DISCONT を検出した frame に伝播。
- `GST_BUFFER_FLAG_MARKER`: AU 終端を示す場合に set。
- `GST_VIDEO_BUFFER_FLAG_INTERLACED` / `TFF`: Picture Timing SEI から interlace 情報が分かる場合に set。
- `GstVideoTimeCodeMeta`: Picture Timing SEI から生成、または `update-timecode` 時の入力として使用。
- `GstVideoCaptionMeta` 等: registered user data SEI から抽出される。

### 3.5 event

`sink_event` で独自処理する event:

- `GST_EVENT_CUSTOM_DOWNSTREAM`
  - `GstForceKeyUnit` downstream event を検出すると pending として保持する。
  - 既に pending がある場合は新規 event を無視する。
- `GST_EVENT_FLUSH_STOP`
- `GST_EVENT_SEGMENT_DONE`
  - DTS 関連 state を reset し、`push_codec=TRUE` にして SPS/PPS 再送を準備する。
- `GST_EVENT_SEGMENT`
  - 複雑な time segment / seek では parser 側 timestamp 補間を止める。
  - `GST_SEEK_FLAG_TRICKMODE_FORWARD_PREDICTED` があれば B slice drop を有効化する。
  - SPS/PPS 挿入間隔の基準時刻を reset する。

`src_event` で独自処理する event:

- `GST_EVENT_CUSTOM_UPSTREAM`
  - upstream `GstForceKeyUnit` event を検出し、`all_headers=true` の場合は pending として保持する。
  - event 自体は base class の `src_event` に流す。

それ以外の event は基本的に `GstBaseParse` の実装へ委譲される。

### 3.6 query

明示的に override している query vfunc はない。関連する query 挙動は以下。

- sink caps query は `get_sink_caps` (`gst_h264_parse_get_caps`) で処理する。下流 src peer caps を見て `stream-format` / `alignment` / `parsed` を調整し、下流が好む format を upstream caps の先頭に並べ直す。
- latency は `gst_base_parse_set_latency()` で baseparse に設定される。入力/出力が NAL のまま、または入力が AU alignment の場合はゼロになりやすく、NAL から AU に集約する場合は framerate から 1 frame 分の latency が設定される。
- その他の position/duration/seek などは `GstBaseParse` 側の標準処理に依存する。

## 4. 処理仕様

### 4.1 初期化と reset

`start()` で `GstH264NalParser` を生成し、最小 frame size を 4 byte に設定する。`stop()` で parser を解放する。stream 情報 reset では width/height/fps/PAR、format/alignment、codec_data、SPS/PPS cache、HDR SEI state などを初期化する。

`init()` では `frame_out` adapter を生成し、PTS interpolation と infer_ts を無効化する。H.264 の DTS/PTS や Picture Timing SEI を element 側で扱うため、baseparse の単純な補間には寄せていない。

### 4.2 caps negotiation

`set_caps()` は以下の順で処理する。

1. upstream caps から width/height/framerate/PAR を取り込む。
2. `stream-format` / `alignment` を読み取る。欠落時は互換用に推定する。
3. `avc` の場合、`codec_data` 必須かつ `alignment=au` を検証する。
4. `byte-stream` の場合、`codec_data` があれば拒否する。
5. `codec_data` があれば avcC を解析し、SPS/PPS を内部 cache に保存する。
6. 入力形式をもとに、下流 allowed caps を参照して出力 `format/alignment` を決める。
7. 入力と出力が同じならすぐ src caps を更新する。
8. AVC/AVC3 から別形式へ変換する場合は `push_codec=TRUE` とし、必要なら packetized input を NAL 単位へ split する。

`negotiate()` は下流が upstream caps をそのまま受けられる場合に `can_passthrough=TRUE` とするが、実際の passthrough 切替コードは `#if 0` で無効化されている。コメント上の理由は、NAL aligned multiresolution stream や MVC stream で caps 更新できなくなる問題である。

### 4.3 NAL 解析

`process_nal()` は NAL type ごとに以下を行う。

- SPS / subset SPS
  - parser state を更新し、src caps 更新を要求する。
  - SPS NAL を id 別 cache に保存する。
  - `have_sps`, `have_sps_in_frame`, `header` を更新する。
- PPS
  - SPS が既にあることを要求する。
  - PPS NAL を id 別 cache に保存する。
  - `have_pps`, `have_pps_in_frame`, `header` を更新する。
- SEI
  - SPS が既にあることを要求する。
  - Picture Timing, registered user data, buffering period, recovery point, stereo/multiview, frame packing, HDR SEI などを解析する。
  - 必要に応じて caps 更新、keyframe 判定、timecode 更新位置記録を行う。
- slice / IDR slice
  - SPS/PPS が既にあることを要求する。
  - slice header から I/P/B 判定、field_pic_flag、frame_start を更新する。
  - IDR または SPS/PPS 挿入が必要な場合、挿入位置 `idr_pos` を記録する。
- AUD
  - AUD が存在することを記録し、byte-stream 出力時の重複挿入を避ける。
- その他
  - 初期 SPS より前の NAL は drop される。
  - NAL parser が失敗した NAL は無効として扱われる。

出力形式変換が必要な場合、各 NAL は `wrap_nal()` で length prefix または start code に包み直され、`frame_out` adapter に蓄積される。

### 4.4 byte-stream 入力の frame 境界検出

`handle_frame()` は byte-stream 入力で start code を探し、NAL を順次識別する。先頭ゴミは `skipsize` で読み飛ばす。壊れた NAL が先頭にある場合は skip し、途中にある場合は現在 AU を終端させる。

AU 完了判定は `collect_nal()` で行う。概略は以下。

- picture が始まった後に SEI/SPS/PPS/AUD など次 AU に属しうる NAL が来たら、前 AU 完了とみなす。
- coded slice で `first_mb_in_slice == 0` と推定できる場合、新 picture の開始とみなす。
- 仕様完全準拠の厳密 AU 判定ではなく、実運用上の壊れた stream にも対応しやすい軽量判定である。

`alignment=nal` 出力なら NAL 1 個で frame を finish する。`alignment=au` 出力なら AU 完了まで入力を蓄積する。入力 caps が `alignment=au` の場合は入力 buffer 全体を消費する。

### 4.5 packetized AVC/AVC3 入力

`handle_frame_packetized()` は `packetized=TRUE` の場合に使われ、`nal_length_size` に従い `gst_h264_parser_identify_nalu_avc()` で NAL を識別する。通常は AVC caps の `codec_data`、または `codec_data` 付き AVC3 caps の avcC 解析でこの経路に入る。

- 出力が NAL alignment の場合は packetized input を NAL 単位に split できる。
- 出力が AU alignment の場合は入力 packet buffer 全体を 1 frame として扱う。
- AVC には AUD がないため、byte-stream 出力時に `pre_push_frame()` で AUD を挿入できるよう `aud_insert=TRUE` にする。
- split 中に壊れた AVC data があると error を返す。

### 4.6 src caps 更新

`update_src_caps()` は、SPS と既存 sink caps から src caps を作る。

SPS 由来:

- crop 後 width/height
- framerate
- pixel aspect ratio
- colorimetry
- chroma-format
- luma/chroma bit depth
- profile / level

SEI 由来:

- multiview-mode / multiview-flags
- mastering-display-info
- content-light-level

AVC/AVC3 AU 出力の場合は `make_codec_data()` で avcC buffer を生成する。AVC3 では SPS/PPS は in-band 前提なので、`codec_data` 内の SPS/PPS 個数は 0 にされる。

既に frame を push 済みの場合、`codec_data` だけが変わっても caps 再送で下流を混乱させないよう、旧 `codec_data` を caps 比較に使う。その場合は `push_codec=TRUE` として次の IDR で in-band SPS/PPS 更新を行う。

### 4.7 parse_frame

`parse_frame()` は frame 出力前に以下を行う。

- src caps 更新。
- DTS/duration 補間。複雑な segment では `do_ts=FALSE` になり、上流 timestamp を尊重する。
- duration が無効なら SPS/SEI 情報から推定。
- keyframe 判定に基づき `DELTA_UNIT` を制御。
- trickmode 時の B frame drop。
- `HEADER`, `DISCONT`, `MARKER` flag を設定。
- format 変換済み data が `frame_out` adapter にあれば、metadata をコピーした replacement buffer を `frame->out_buffer` に設定する。

### 4.8 pre_push_frame

`pre_push_frame()` は実際に src pad へ push される直前の処理を行う。

1. 最初の frame で codec tag を merge する。
2. byte-stream 出力で AUD が必要なら挿入する。
   - AU alignment では buffer 先頭に AUD NAL を prepend。
   - NAL alignment では AUD を別 buffer として push。
3. pending `GstForceKeyUnit` がある場合、keyframe 到達時に downstream force-key-unit event を発行し、`push_codec=TRUE` にする。
4. `update-timecode` が有効なら Picture Timing SEI を更新した buffer に差し替える。
5. `config-interval` または `push_codec` に応じて SPS/PPS を送る。
   - NAL alignment では SPS/PPS を別 buffer として push。
   - AU alignment では IDR 位置の前に SPS/PPS を挿入して buffer を差し替える。
6. Picture Timing SEI から `GstVideoTimeCodeMeta` を付与する。
7. interlace flags と user data meta を出力 buffer に反映する。
8. frame 単位の一時 state を reset する。

## 5. 制約

### 5.1 入力 caps 制約

- `stream-format=avc` は `codec_data` 必須。
- `stream-format=avc` は `alignment=au` のみ許容。
- `stream-format=byte-stream` に `codec_data` がある caps は拒否される。
- `codec_data` は buffer 型でなければならない。
- avcC は version 1 でなければならない。
- `nal_length_size` は 1..4 byte でなければならない。

### 5.2 解析順序の制約

- PPS は SPS 後でなければ処理できない。
- SEI は SPS 後でなければ処理できない。
- slice は SPS/PPS 後でなければ処理できない。
- 初期 SPS より前の NAL は drop される。
- NAL alignment 出力では、SPS/PPS が揃うまで queue し、caps が不完全なまま push しない。

### 5.3 AU 判定の制約

AU 境界検出は H.264 spec の全条件を厳密に実装するよりも、軽量で壊れた stream にも耐える判定に寄せている。`frame_num` などを完全に検査する方式ではないため、特殊な slice 構成や壊れた stream では意図と違う AU 境界になる可能性がある。

### 5.4 timecode 更新の制約

`update-timecode` は既存 Picture Timing SEI を書き換える機能であり、SEI が存在しない stream に新規 SEI を挿入しない。さらに、VUI の `pic_struct_present_flag` が有効でない場合は必要情報が不足するため更新できない。実装上も、1 SEI NAL に複数 message がある場合の部分更新は未対応として扱われている。

### 5.5 caps 更新の制約

stream 中盤で `codec_data` だけが変わった場合、caps 再送は downstream decoder を混乱させうるため抑制される。その代わり、次 IDR に SPS/PPS を in-band 挿入する。独自機能で caps field を追加/変更する場合、frame push 後の caps 再送と in-band 補完のどちらに乗せるべきかを明確にする必要がある。

### 5.6 passthrough の制約

実装には `can_passthrough` state があるが、passthrough 切替は無効化されている。理由は、NAL aligned multiresolution stream や MVC stream で解像度などの caps 変化を通知できなくなるためである。独自機能で高速化を目的に passthrough を戻す場合、SEI/caps 更新、SPS/PPS 再送、multiview、resolution change の回帰リスクが大きい。

### 5.7 latency の制約

NAL から AU へ集約する場合、次 AU の開始を見るまで前 AU を確定できないため、1 frame 程度の latency が入る。テストでも `alignment=nal -> au` では 30 fps 時に 1 frame latency が期待されている。一方、入力が AU alignment、または NAL 入出力のままならゼロ latency になるケースがある。

### 5.8 メモリ/バッファ制約

format 変換、AUD 挿入、SPS/PPS 挿入、SEI 更新では replacement buffer を作る。metadata はコピーされるが、memory layout は変わる。独自機能で DMA-BUF や zero-copy を意識する場合、`frame->out_buffer` 差し替えや `gst_buffer_copy_into()` の意味を確認する必要がある。

## 6. 独自機能追加時の設計ポイント

追加機能の種類ごとに、主な挿入点は以下になる。

| 追加したい機能 | 主な挿入点 | 注意点 |
| --- | --- | --- |
| 新しい SEI の解析 | `gst_h264_parse_process_sei()` | caps 更新が必要なら `update_caps=TRUE` と `update_src_caps()` の field 追加も必要。 |
| 新しい SEI の出力 buffer meta 化 | `pre_push_frame()` 後半 | 既存 user data / timecode meta 付与と同じく、最終 `parse_buffer` に付ける。 |
| 既存 NAL の書き換え | `pre_push_frame()` | AUD/SPS/PPS/SEI 更新との順序、`idr_pos` 補正、metadata コピーが必要。 |
| 新しい caps field | `update_src_caps()` | upstream caps の field を尊重するか、SPS/SEI 由来で上書きするかを決める。 |
| format/alignment 追加 | pad template, enum, `format_from_caps()`, `negotiate()`, `wrap_nal()`, `set_caps()` | テストの組み合わせが増える。packetized/byte-stream 両方の変換経路が必要。 |
| keyframe / random access 関連 | `process_nal()`, `pre_push_frame()`, force-key-unit event 処理 | `DELTA_UNIT`, SPS/PPS 再送、pending event の seqnum 継承を崩さない。 |
| low-latency 動作 | `handle_frame()`, `collect_nal()`, latency 設定 | AU 完了判定と caps 完成待ちの queue 挙動を変えるため回帰リスクが高い。 |

このリポジトリの codec plugin に関係する観点では、`h264parse` の責務を decoder/encoder 内へ取り込むよりも、引き続き前後段に `h264parse` を置き、ローカル element は `byte-stream,alignment=au` を前提に保つ方が実装面のリスクは低い。独自機能が parser の責務に入る場合は、`h264parse` 相当の state machine を local plugin 内に複製するのではなく、上流 `h264parse` のどの段階に hook するか、または別 element として parser 後段に置けるかを先に検討する。

## 7. ソース照合チェック

資料作成後、以下の観点で上流ソースと再照合した。

- pad template は `sink=video/x-h264`、`src=video/x-h264,parsed=true,stream-format={avc,avc3,byte-stream},alignment={au,nal}` で一致。
- property は `config-interval` と `update-timecode` の 2 つで一致。
- `GstBaseParse` vfunc override は `start`, `stop`, `handle_frame`, `pre_push_frame`, `set_sink_caps`, `get_sink_caps`, `sink_event`, `src_event` で一致。
- `set_caps()` の拒否条件として、AVC の `codec_data` 必須、AVC の AU alignment 必須、byte-stream の `codec_data` 拒否を確認。
- `update_src_caps()` が SPS, VUI, SEI, upstream caps, codec_data を使って src caps を生成することを確認。
- `parse_frame()` が caps 更新、timestamp/duration、buffer flags、B frame drop、replacement buffer を処理することを確認。
- `pre_push_frame()` が AUD 挿入、force-key-unit、timecode SEI 更新、SPS/PPS 挿入、timecode/user data meta 付与を処理することを確認。
- `sink_event` / `src_event` の独自処理は force-key-unit、flush/segment、trickmode forward predicted に限定され、それ以外は base class へ委譲されることを確認。
- テストは通常 parsing、drain、garbage skip、stream 検出、HDR SEI caps、caps reordering、profile compatibility、packetized input、NAL/AU alignment 変換、caption SEI、start code split、複数出力形式 (`byte-stream nal`, `byte-stream au`, `avc au`, `avc3 au`) を確認している。

## 8. 参照箇所メモ

- `gsth264parse.c:88-98`: pad template
- `gsth264parse.c:144-174`: property 定義
- `gsth264parse.c:176-185`: `GstBaseParse` vfunc override
- `gsth264parse.c:313-345`: start/stop
- `gsth264parse.c:376-474`: caps から format/alignment を読み、下流 caps と negotiation
- `gsth264parse.c:505-539`: SPS/PPS NAL cache
- `gsth264parse.c:608-925`: SEI 解析
- `gsth264parse.c:930-1155`: NAL type 別処理
- `gsth264parse.c:1160-1193`: AU 完了判定
- `gsth264parse.c:1202-1305`: packetized AVC/AVC3 入力処理
- `gsth264parse.c:1307-1585`: byte-stream 入力の handle_frame
- `gsth264parse.c:1587-1678`: AVC/AVC3 `codec_data` 生成
- `gsth264parse.c:2015-2404`: src caps 更新
- `gsth264parse.c:2628-2700`: parse_frame
- `gsth264parse.c:2723-2816`: force-key-unit と SPS/PPS 再送準備
- `gsth264parse.c:2818-2906`: SPS/PPS 挿入
- `gsth264parse.c:2908-3074`: Picture Timing SEI 書き換え
- `gsth264parse.c:3076-3333`: pre_push_frame
- `gsth264parse.c:3336-3580`: set_caps
- `gsth264parse.c:3600-3640`: get_caps
- `gsth264parse.c:3643-3753`: sink/src event
- `gsth264parse.h:54-163`: `GstH264Parse` 内部状態
- `tests/check/elements/h264parse.c:358-395`: normal/drain/garbage/split
- `tests/check/elements/h264parse.c:408-516`: stream 検出と HDR SEI caps
- `tests/check/elements/h264parse.c:517-558`: caps reordering
- `tests/check/elements/h264parse.c:903-1120`: NAL/AU alignment 変換、AUD、latency、DISCONT
- `tests/check/elements/h264parse.c:1171-1219`: caption SEI
- `tests/check/elements/h264parse.c:1221-1255`: start code split
- `tests/check/elements/h264parse.c:1267-1340`: 出力形式別 suite 設定
