# pmxer

pmxer は、PMX 2.0 と 2.1 のモデルを編集するクロスプラットフォームのデスクトップアプリです。
モデルの検証、3D プレビュー、保存に加え、コマンドラインから情報表示や比較を行えます。

## 主な機能

- 頂点、ウェイト、材質、テクスチャ、ボーン、IK、モーフ、表示枠を編集できます。
- 剛体、ジョイント、SoftBody を編集し、対応範囲の診断を確認できます。
- Undo/Redo、参照調査、検証診断、基準モデルとの差分、回復保存を利用できます。
- Transform View で頂点やボーンを一時変形し、頂点モーフやボーンモーフを作成できます。
- モーフミキサーでモーフを組み合わせ、反転、倍率変更、左右分割、材質マスク、ベイクを行えます。
- 3D ビューポートで材質、面、ボーン、剛体、ジョイントを確認できます。MMD 形式のモーションやポーズを読み込み、IK と物理を含むプレビューもできます。
- PNG などの一般画像と、非圧縮 RGB(A)、BC1、BC2、BC3、BC4、BC5、BC7 の DDS テクスチャを表示できます。

保存時は一時ファイルを作成し、再読み込み後の意味比較に成功してから対象ファイルと原子的に
置き換えます。Windows で作られたモデルでは、テクスチャ名の大文字・小文字だけが異なる場合も
読み込めます。候補が複数ある場合は自動選択しません。

## ビルドと起動

### 必要なツール

- CMake 3.25 以上
- Ninja
- C++20 対応コンパイラ
- `make` を使う場合は Git
- 初回ビルドで依存ライブラリを取得する場合はネットワーク接続

`Makefile` は CMake preset を使用します。`make` は `external/libmmd` サブモジュールを
初期化してから、既定の `linux-dev` preset で GUI アプリをビルドします。この preset は
SDL3 などの GUI 依存ライブラリも取得します。

```sh
git clone https://github.com/haturatu/pmxer.git
cd pmxer
make
make run ARGS="model.pmx"
```

よく使う操作は次のとおりです。

```sh
make test
make cli
make sanitize
make release JOBS=16
make run ARGS="info model.pmx"
make help
```

CLI だけをビルドする場合は `make cli` を実行します。CMake を直接使う場合は、
`CMakePresets.json` の `linux-dev`、`linux-cli`、`linux-clang`、`linux-release`、
`windows-release`、`macos-arm64-release`、`macos-x86_64-release` preset を利用できます。

## CLI

ビルドした `pmxer` は、モデル情報の表示、検証、差分比較、ウェイトの正規化に対応します。

```sh
build/linux-cli/pmxer info model.pmx
build/linux-cli/pmxer validate --json model.pmx
build/linux-cli/pmxer diff --profile preservation before.pmx after.pmx
build/linux-cli/pmxer normalize -o normalized.pmx model.pmx
```

各コマンドのオプションは `pmxer help` または `pmxer help <command>` で確認できます。CLI は GUI なしでビルドできます。

GUI の起動時は `--lang` で表示言語を指定できます。日本語 (`ja`)、英語 (`en`)、
中国語・簡体字 (`zh-CN`)、ロシア語 (`ru`) に対応しています。たとえば
`pmxer --lang ru model.pmx` でロシア語表示になります。フォントを自動検出できない場合は
`--font` または `PMXER_FONT` で指定してください。

## AI 編集

AI 編集パネルは、自然言語の指示から選択範囲の編集案を作成します。利用には
[Codex CLI](https://developers.openai.com/codex/cli) と ChatGPT アカウントへのログインが
必要です。認証情報は Codex が管理し、pmxer はトークンを保存しません。API キーによるログインには
対応していません。Codex CLI を自動検出できない場合は、実行ファイルのパスを
`PMXER_CODEX` に指定してください。

1. PMX モデルを開き、`AI編集`（Ctrl+J）を選びます。
2. ビューポートで範囲を選択するか、モデル全体から対象を探す設定を使います。
3. 編集内容を入力し、プレビューを生成します。
4. 対象とプレビューを確認して編集を適用します。プレビューを破棄するか Undo で元に戻せます。

編集案では、色の変更、移動・回転・拡縮、メッシュの細分化、平滑化、膨張・収縮、基本形状の
衣装や靴の生成を行えます。細分化は選択した三角形を 4 つに分割します。画像テクスチャの解像度を
上げる処理ではありません。対象外の面を保持し、生成メッシュには近傍頂点からウェイトを転送します。

生成できる基本形状はローファー、ブーツ、半袖シャツ、長袖コート、プリーツスカート、
ワンピース、ズボンです。任意デザインのメッシュやテクスチャの生成、身体の補完、自動衝突回避、
衣装の物理設定には対応していません。元モデルに衣装の下の身体がない場合、短い衣装へ変更すると
隙間ができることがあります。

Codex へ送信する情報は、指示、選択範囲の大きさと件数、材質の位置・件数・名前・色・影響ボーンです。
PMX ファイル、頂点配列、テクスチャ画像は送信しません。Codex は読み取り専用の一時スレッドで
構造化された編集案を作り、pmxer が値と対象を検証してから適用します。

### ローカル衣装作成

`ローカル衣装作成` では、色、段数、丈、広がり、袖の長さなどを指定してゴスロリ衣装を
生成できます。衣装の構築に Codex、外部サービス、API キー、学習済みモデル、GPU 推論は
必要ありません。自然言語で設定する場合は Codex 連携を使います。

衣装は身頃、袖、襟、段スカート、フリル、レース縁、リボンなどの部品で構成します。部品ごとに
選択して編集でき、ウェイトは元の衣装表面から転送します。生成した編集は Undo で取り消せます。
元モデルに衣装の下の腕がない場合、短袖や袖なしに変更しても身体は補完されません。

## テストと検証

通常のテストは `make test` で実行します。Linux のローカル CI と同じビルド・テスト手順には `make ci` を使います。

Codex を使った実通信の確認には GUI 版の `pmxer_ai_smoke` を利用できます。既存の Codex 認証と利用枠を使い、入力モデルを変更せず、出力先に PMX ファイルと編集計画を保存します。

```sh
./build/linux-dev/pmxer_ai_smoke --live model.pmx build/ai-live
./build/linux-dev/pmxer_ai_smoke --replay model.pmx build/ai-live
```

ローカル衣装生成は認証なしで検証できます。

```sh
./build/linux-release/pmxer_ai_smoke --local-outfit model.pmx build/local-outfit
```

## ログと終了コード

DEBUG と INFO は標準出力へ、WARN と ERROR は標準エラーへ出力します。検証結果の FATAL は
標準エラーへ出力し、検証不合格として終了コード `1` を返します。CLI の終了コードは、成功 `0`、
検証不合格 `1`、引数エラー `2`、入出力エラー `3` です。

## ライセンス

本体のライセンスと依存ライブラリのライセンスは配布物に同梱します。詳細は [LICENSE](LICENSE) と
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
