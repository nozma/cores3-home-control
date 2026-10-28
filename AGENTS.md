# 開発ガイド

- 対象デバイスは M5Stack CoreS3 SE。
- 開発環境は PlatformIO Core + Arduino + M5Unified。バージョンは `requirements.txt` と `platformio.ini` で固定する。
- READMEはプロジェクト概要を簡潔に記載し、Codex向けの開発手順や注意事項はこのファイルで管理する。

## 設定情報の管理

- Wi-FiのSSID・パスワード、APIトークンなどはコミットしない。
- ローカル設定には `.env` や `secrets.h` など、Gitの追跡対象から除外したファイルを使用する。
- 設定例を共有するときは、実際の認証情報を含まない `.env.example` などを使用する。

## 開発手順

リポジトリのルートで実行する。

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/pio run
.venv/bin/pio device list
.venv/bin/pio run -t upload --upload-port /dev/cu.usbmodemXXXXX
.venv/bin/pio device monitor --port /dev/cu.usbmodemXXXXX --baud 115200
```

- PlatformIOのツール類は `.platformio/`、ビルド成果物は `.pio/` に保存する。いずれもコミットしない。
- USBポート名は接続ごとに確認し、共有設定には固定しない。
- CoreS3 SEは `m5stack-cores3` ボード設定を使用する。周辺機器の初期化はM5Unifiedに任せる。
- アプリの入口は `src/main.cpp`。Wi-Fi状態を表示し、`src/lighting.cpp` で赤外線照明のタッチ操作とRemo通信を扱う。
- ビルド成功と実機動作確認を区別して報告する。画面の見た目はユーザーによる確認が必要。
- 書き込み接続に失敗する場合は、RSTボタンを約3秒押し、緑LEDが点いたら離してダウンロードモードにする。その後ポートを再確認する。

## Wi-Fi

- `include/secrets.example.h` を `include/secrets.h` にコピーし、2.4GHzのSSIDとパスワードを設定してからビルド・書き込みする。
- `include/secrets.h` はGit管理外。内容をログ、チャット、コミットに出力しない。サンプルは空の値を維持する。
- 設定がない場合もビルドでき、画面には `Wi-Fi: Not configured` を表示する。
- 接続試行は15秒で打ち切り、5秒待って再試行する。接続待ちでメインループを止めない。
- 認証情報をフラッシュのWi-Fi設定に永続化しない。ただしビルドしたファームウェアには含まれるため、`.pio/` の成果物は公開しない。

## 赤外線照明（Nature Remo）

- `include/remo_secrets.example.h` を `include/remo_secrets.h` にコピーし、トークンと電源・明るく・暗くの信号送信URLを設定する。実値はGit管理外に置き、出力しない。
- Cloud APIのHTTPS証明書を `include/remo_ca.h` で検証する。証明書検証に必要な時刻はNTPで取得する。`setInsecure()` は使わない。
- 電源は点灯→常夜灯→消灯→点灯。起動時は未確認。実状態は取得できず、画面の状態はすべて推定。
- Remo上の対象家電名は「キッチン」。「オフ」信号も登録されているが電源と同じ1回分の信号なので、独立した消灯信号として使用しない。
- 「点灯中に合わせる（最大へ）」で状態・明るさ合わせを一括実行する。実際に点灯している状態で押すことが前提。送信開始時に点灯と仮定し、明るくする信号を4回送って、全送信成功後に明るさの推定値を最大に合わせる。電源信号は送らない。「電源1回」は状態未確認でも送信できる。
- 照明は消灯・常夜灯でも明るさ設定を保持する。電源操作・電源送信失敗では明るさの推定値を維持する。調光送信失敗時は明るさを未確認に戻す。消灯・常夜灯・電源未確認の間は通常の調光をしない。
- 調光は20〜100の5段階（ユーザー確認済み）。合わせ操作は端から端までに必要な4回だけ送信する。回数は `include/light_model.h` の `kSyncPresses` で定義し、余分な送信はしない。
- 通信はワーカータスクで直列実行し、信号間隔は1秒以上。送信中の追加操作を受け付けない。
- 失敗時は処理を打ち切り、影響する推定値を未確認に戻す。電源信号を自動再送しない。HTTP成功も赤外線受信成功を保証しない。
- APIの30リクエスト/5分制限を端末内で抑制し、HTTP 429時は5分待つ。同じトークンを使う他端末の利用量は把握できない。
- Hueと時刻別自動制御は未実装。以前の定期実行はRaspberry Piのcrontabで設定していた。
- 状態遷移のテスト：`c++ -std=c++17 -I include tests/light_model_test.cpp -o /tmp/cores3-light-model-test && /tmp/cores3-light-model-test`

## Alexa

- Espalexa 2.7.0でLAN内に「キッチン照明」を調光対応ライトとして公開する。Node-REDやRaspberry Piは使わない。Alexa側の仕様・Echo機種との互換性は実機で確認する。
- EchoとCoreS3 SEを同じLANに接続し、Alexaアプリからデバイスを検出する。SSDP（UDP 1900）とHTTP（TCP 80）を使う。
- 音声操作はオン・オフと明るさ指定。0は消灯、1〜100%は20刻みの最寄りの段階（20〜100）に丸める。常夜灯の音声指定は未対応。
- 画面とAlexaは同じ推定状態・送信キューを使う。再起動後など状態が未確認なら、先に実際の照明を点灯し、画面の合わせボタンで初期設定する。
- 赤外線信号は必要な回数だけ直列送信する。音声操作で点灯後の明るさを指定する場合も、電源操作後に調光する。Alexaが点灯と明るさを別要求で送る場合は、送信中でも後続の目標状態を保持し、進行中の処理が終わってから最新の目標を実行する。同一指示の重複で信号を再送しない。
- Espalexaの応答は受付応答であり、照明への送信成功を保証しない。失敗・未確認状態は実機画面で確認する。Alexaには未確認状態の表現がなく、最後の表示値が残る場合がある。
- Espalexa標準の操作応答は固定の照明IDを使うため、`src/alexa_control.cpp` の外部WebServerでPUTを処理し、要求された照明IDとon/bri各属性に対応するHue形式の応答を返す。送信完了までは受付済みの状態をGETに返し、重複した同じ操作で信号を再送しない。
