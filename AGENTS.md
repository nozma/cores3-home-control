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
- アプリの入口は `src/main.cpp`。画面にWi-Fi接続状態、接続時のIPアドレスと電波強度を表示する。
- ビルド成功と実機動作確認を区別して報告する。画面の見た目はユーザーによる確認が必要。
- 書き込み接続に失敗する場合は、RSTボタンを約3秒押し、緑LEDが点いたら離してダウンロードモードにする。その後ポートを再確認する。

## Wi-Fi

- `include/secrets.example.h` を `include/secrets.h` にコピーし、2.4GHzのSSIDとパスワードを設定してからビルド・書き込みする。
- `include/secrets.h` はGit管理外。内容をログ、チャット、コミットに出力しない。サンプルは空の値を維持する。
- 設定がない場合もビルドでき、画面には `Wi-Fi: Not configured` を表示する。
- 接続試行は15秒で打ち切り、5秒待って再試行する。接続待ちでメインループを止めない。
- 認証情報をフラッシュのWi-Fi設定に永続化しない。ただしビルドしたファームウェアには含まれるため、`.pio/` の成果物は公開しない。
