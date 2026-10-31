# pmxer

pmxer は、PMX 2.0 / 2.1 モデルの編集・検証・保存と、モーションを使ったプレビューを行うデスクトップアプリケーションです。GUI に加え、モデル情報の表示、検証、差分比較、ウェイト正規化を行うコマンドライン機能も備えています。

Linux、Windows、macOS 向けの C++20 アプリケーションです。PMX / VMD などのファイル処理には [libmmd](https://github.com/haturatu/libmmd) を使用します。

## インストールと起動

### 必要環境

| 項目 | 要件 |
| --- | --- |
| ビルドシステム | CMake 3.25 以上、Ninja |
| コンパイラ | C++20 対応 |
| GUI | SDL3 の開発ファイル |
| ソース取得 | Git（submodule を使用） |
| 依存関係の取得 | 初回の CMake configure 時にネットワークが必要になる場合あり |

GUI と物理プレビューには追加の依存ライブラリが必要です。`linux-dev` / `linux-release` preset では必要な GUI 依存ソースの取得を有効にしています。CLI のみを使う場合は `linux-cli` preset を使用してください。

### Linux でビルドする

リポジトリを取得して、Debug 版をビルドします。

```sh
git clone --recurse-submodules https://github.com/haturatu/pmxer.git
cd pmxer
make
```

`make` の既定ターゲットは `build` で、`linux-dev` preset の設定とビルドを実行します。`external/libmmd` が初期化されていなければ、ビルド前に submodule を取得します。

ビルド済みの GUI は直接起動できます。

```sh
./build/linux-dev/pmxer
./build/linux-dev/pmxer /path/to/model.pmx
```

`/path/to/model.pmx` は実際のファイルパスに置き換えてください。複数の PMX を指定すると、複数文書として開けます。

**注記:** `make run ARGS="..."` は再度 configure / build してから起動する開発用ターゲットです。すでにビルドしたアプリケーションの通常の起動には不要です。

### Release 版と CLI 専用版

Release GUI と GUI を含まない CLI は別のビルドディレクトリに生成されます。

```sh
make release
./build/linux-release/pmxer /path/to/model.pmx

make cli
./build/linux-cli/pmxer info /path/to/model.pmx
```

CLI 専用版は GUI や SDL3 の描画画面を起動しません。通常の GUI が不要なバッチ処理・CI ではこちらを使います。

ほかのターゲットは `make help` で確認できます。`make sanitize` は Clang と AddressSanitizer / UndefinedBehaviorSanitizer を有効にした CLI ビルドです。

### CMake を直接使用する

Makefile は CMake Presets のラッパーです。CMake のみでビルドすることもできます。

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev --output-on-failure
```

使用可能な preset は `cmake --list-presets=all` または `make presets` で確認してください。

| Preset | 用途 | GUI |
| --- | --- | --- |
| `linux-dev` | Linux Debug（既定） | 有効 |
| `linux-cli` | Linux Debug、CLI 専用 | 無効 |
| `linux-clang` | Linux Debug、Clang と sanitizer | 無効 |
| `linux-release` | Linux Release | 有効 |
| `windows-release` | Windows Release | 有効 |
| `macos-arm64-release` | macOS arm64 Release | 有効 |
| `macos-x86_64-release` | macOS x86_64 Release | 有効 |

Windows / macOS では各プラットフォームで必要な依存ライブラリと開発環境を別途用意してください。上記 preset の存在は、すべての構成で動作確認が完了していることを意味しません。

## PMX の編集

### ファイルを開く

起動引数に PMX を指定するか、GUI からファイルを選択します。複数文書タブを使ってモデルを切り替えられます。3D ビューポートでは回転・ズームし、モデルの形状と各種編集結果を確認できます。

主な編集対象は次のとおりです。

| 分類 | 編集・確認できる内容 |
| --- | --- |
| 頂点とウェイト | BDEF1 / BDEF2 / BDEF4、SDEF、QDEF、ウェイト正規化 |
| 材質 | 色、面、テクスチャ、材質設定 |
| ボーン | 階層、付与、IK、準標準ボーンのレシピ |
| モーフ | 頂点・ボーンモーフ、Morph Mixer、変形の反転・倍率・左右分割 |
| 物理 | 剛体、ジョイント、SoftBody と物理チェーン |
| 構造 | モデル結合、参照調査、意味差分と検証診断 |

材質・ボーン・剛体・ジョイント・SDEF マーカーは同じビューポート内で確認できます。対応する画像形式は PNG などの一般画像形式に加え、非圧縮 RGB(A) と BC1 / BC2 / BC3 / BC4 / BC5 / BC7 の DDS です。すべての DDS 形式に対応するわけではありません。

### 安全な保存と編集履歴

pmxer は、編集対象の参照に安定ハンドルを使用し、構造変更をトランザクションとして確定します。Undo / Redo、参照調査、検証診断、基準モデルとの差分確認を同じ編集基盤から提供します。

保存処理では、変更内容を検証して一時ファイルに書き込み、再読み込み後の意味比較を行います。検証に成功した場合だけ、保存先のファイルを原子的に置き換えます。既定の保持モードでは元モデルを直接上書きしない設計です。

**注意:** 検証成功は、すべての MMD 互換アプリケーションで見た目や物理挙動が同一になることを保証しません。編集前のモデルとテクスチャは別途バックアップしてください。

### Transform View

`Transform View` は、ビューポートと連携して頂点・ボーンを一時変形させ、PMX のモーフへ反映する作業領域です。

#### 頂点モーフを作成する

1. `Transform View` の `頂点` タブを選択します。リグワークスペースと頂点選択に切り替わります。
2. ビューポートまたはパネルの `選択`、`移動 G`、`回転 R`、`拡縮 S` で頂点を編集します。
3. 元の基準位置を表示する薄い点・線を参考に変形量を確認します。
4. `頂点モーフを作成` を実行して PMX へ反映します。

モーフを作成するまでは変形は一時状態であり、PMX 本体へは保存されません。

#### ボーンモーフを作成する

1. `ボーン` タブに切り替えます。リグワークスペースでボーン選択が有効になります。
2. ボーンを 1 本ずつ選択・変形します。複数のボーンを順番に変形して一つのモーフにまとめることもできます。
3. 制御対象の確認後、ボーンモーフを作成します。

IK、付与、物理などで制御されるボーンは操作できないことがあり、その理由は画面に表示されます。

頂点とボーンの一時変形は別々に管理されます。各タブの破棄操作はその種類だけを対象にし、全体の破棄では確認画面を表示します。

#### Morph Mixer と高度な操作

`モーフ` タブでは、Morph Mixer からグループモーフを作成したり、頂点モーフへベイクしたりできます。反転、倍率変更、結合、減算、左右分割、材質マスクは `高度なモーフ操作` にまとめられています。各 PMX 変更は一回の Undo で取り消せます。

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


## コマンドライン

ビルドに使用した preset に応じて、以下の `./build/linux-cli/pmxer` を `./build/linux-dev/pmxer` などに置き換えられます。

### 情報表示・検証・比較

```sh
# モデルの情報を表示
./build/linux-cli/pmxer info model.pmx

# 検証結果を JSON で出力
./build/linux-cli/pmxer validate --json model.pmx

# 二つのモデルの意味差分を表示
./build/linux-cli/pmxer diff before.pmx after.pmx

# 元データの保持を重視する比較
./build/linux-cli/pmxer diff --profile preservation before.pmx after.pmx

# ウェイトを正規化し、別ファイルへ出力
./build/linux-cli/pmxer normalize -o normalized.pmx model.pmx
```

`info`、`validate`、`diff`、`normalize` は `--json` に対応します。`validate` は複数のモデルファイルを受け付けます。

| コマンド | 用途 |
| --- | --- |
| `info FILE` | モデルの情報を表示 |
| `validate FILE...` | PMX の検証。複数指定可能 |
| `diff [--profile logical\|preservation] BEFORE AFTER` | 二つの PMX を比較。既定は `logical` |
| `normalize [-o OUTPUT] FILE` | モデルのウェイトを正規化 |
| `help [COMMAND]` | ヘルプ |
| `edit [OPTIONS] [FILE...]` | GUI を起動。`edit` は省略可能 |

テキスト以外の機械可読な結果が必要な場合は `--json` を指定してください。コマンド固有の詳細は `pmxer help diff` のように確認できます。

### GUI 起動オプション

```sh
./build/linux-dev/pmxer --lang ja --renderer auto model.pmx
./build/linux-dev/pmxer --safe-mode --no-physics model.pmx
./build/linux-dev/pmxer --font /path/to/font.ttf --font-size 18
```

| オプション | 動作 |
| --- | --- |
| `--renderer auto\|vulkan\|direct3d12\|metal` | 描画バックエンドの選択 |
| `--gpu-debug` | GPU validation を有効化 |
| `--no-physics` | 物理プレビューを無効化 |
| `--safe-mode` | 保守的な設定で起動。物理プレビューも無効化 |
| `--font PATH` / `--font-size SIZE` | UI フォントとサイズを指定 |
| `--resource-dir PATH` | アプリケーションリソースの場所を指定 |
| `--lang auto\|ja\|en\|zh-CN\|ru` | GUI 言語を指定 |

描画バックエンドは OS と GPU の対応状況によって利用可否が異なります。`--renderer` で選べる値がすべての OS で動作するとは限りません。

### 表示言語とフォント

GUI は日本語、英語、簡体字中国語、ロシア語に対応します。既定の `--lang auto` は環境変数 `PMXER_LANG`、`LC_ALL`、`LC_MESSAGES`、`LANG`、OS の優先言語の順に確認し、未対応の言語では英語を使います。`ja-JP`、`en_US.UTF-8`、`zh-Hans` などの locale 形式も受け付けます。

翻訳対象は GUI の文字列です。PMX 内の名前・コメントや JSON のキー名は変更されません。日本語・中国語・キリル文字を表示するには、Noto Sans CJK など適切な字形を含むフォントを使用してください。自動検出できない場合は `--font` または `PMXER_FONT` で指定できます。リソースディレクトリは `PMXER_RESOURCE_DIR` でも指定できます。

言語を切り替えてもドック配置は維持され、初回起動時には旧形式のレイアウトがバックアップされます。

## 開発・テスト

### テストの実行

```sh
make test
make PRESET=linux-cli test
make ci
```

`make test` は選択した preset のビルド後に CTest を実行します。`make ci` は CLI 構成の警告をエラーとして扱い、ビルドとテストを行います。`make sanitize` は sanitizer を使った不具合調査向けです。テスト実行前に必要な依存関係を取得してください。

### ビルドの並列数とインストール先

```sh
make release JOBS=8
make install PREFIX="$HOME/.local/pmxer"
```

`JOBS` で CMake ビルドの並列数を、`PREFIX` でインストール先を指定できます。現在の preset に対応するビルドディレクトリは `build/<preset>/` です。

### 問題の切り分け

- **libmmd が見つからない:** `git submodule update --init --recursive` または `make setup` を実行します。
- **GUI のビルドで依存ライブラリが不足する:** SDL3 開発ファイルと初回 CMake configure 時のネットワークを確認します。CLI だけが必要なら `make cli` を使用します。
- **PMX の保存が失敗する:** 保存先の権限と空き容量、モデルの検証診断を確認します。一時保存と再読み込み検証が失敗した場合は保存が確定しません。
- **文字が表示されない:** 必要な文字を含むフォントを `--font` で指定します。
- **プレビューが描画されない:** OS に対応するレンダラを指定し、`--gpu-debug` や `--safe-mode` で原因を切り分けます。

診断ログは DEBUG / INFO を標準出力、WARN / ERROR を標準エラーに出力します。終了コードは成功 `0`、検証不合格 `1`、引数エラー `2`、入出力エラー `3`、内部エラー `70` です。

## ライセンス

pmxer 本体は [MIT License](LICENSE) です。依存ライブラリはそれぞれのライセンスに従います。[サードパーティの通知](THIRD_PARTY_NOTICES.md)も確認してください。

PMX、VMD、モデルのテクスチャなどのユーザー素材には、それぞれの配布元の利用条件が適用されます。
