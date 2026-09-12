# pmxer

pmxer は PMX 形式の編集、検証、保存、プレビューを行うクロスプラットフォームのデスクトップアプリケーションです。

## 方針

- PMX の参照を安定ハンドルで扱い、UI から生の添字を変更しない
- 保存前に検証し、一時ファイルへの保存と再読み込み比較を通過した場合だけ原子置換する
- 構造編集はトランザクションとして一度に確定する
- 編集履歴、診断、参照調査、意味差分を同じ編集層から提供する
- Linux、Windows、macOS のネイティブビルドを同じソースから作る

## 対応範囲

- PMX 2.0 / 2.1 の読み込み、検証、保存後の意味比較、原子置換
- 頂点、BDEF1/2/4、SDEF、QDEF、材質、テクスチャ、ボーン、付与、IK、モーフ、表示枠の編集
- 剛体、ジョイント、SoftBody の保存・編集と対応範囲の診断
- SDEF 変換、ウェイト正規化、準標準ボーンのレシピ、物理チェーン生成、モデル追加結合
- Undo / Redo、参照調査、検証診断、基準との差分、回復保存
- MMD形式のモーション・ポーズ読み込みと、IK・物理を含むプレビュー
- SDL3のネイティブファイルダイアログ、複数文書タブ、回転・ズーム可能な3Dビューポート
- 材質色、面、ボーン、剛体、ジョイント、SDEFマーカーを同じビューポートで確認
- PNG等の一般画像と、非圧縮RGB(A)・BC1・BC2・BC3・BC4・BC5・BC7のDDSテクスチャ
- 安定ハンドルとトランザクションを使用した構造編集
- Transform View による一時的な頂点変形・ボーンポーズ編集とモーフ作成
- Morph Mixer、左右分割、変化量の反転・倍率変更、材質マスク、Bake & Reverse

保存は既定で保持モードを使い、元ファイルを直接上書きしません。一時ファイルへ書き出し、再読み込みした内容の意味比較が成功した場合だけ原子置換します。

## 必要環境

- CMake 3.25 以上
- Ninja
- C++20 対応コンパイラ
- SDL3 開発ファイル（GUI ビルド時）
- ネットワーク接続（GUI と物理プレビューの初回ビルドで依存ライブラリを取得する場合）

## ビルド

```sh
git clone <repository-url> pmxer
cd pmxer
make
make test
```

Makefile は CMake preset を呼び出す薄い操作 UI です。初回の `make` では
`external/libmmd` サブモジュールも自動初期化されます。CMakeを直接操作する場合は、
`CMakePresets.json` の `linux-dev`、`linux-cli`、`linux-clang`、
`linux-release` などを利用できます。

```sh
make cli
make sanitize
make run ARGS="model.pmx"
make run ARGS="info model.pmx"
make PRESET=linux-cli test
make release JOBS=16
```

利用できる操作は `make help` で確認できます。

CLI は GUI なしでもビルドできます。

```sh
make cli
make PRESET=linux-cli run ARGS="info model.pmx"
```

GUI を起動する場合は `make run ARGS="model.pmx"`、ビルドの並列数を指定する場合は
`make JOBS=8` のように実行します。CMake preset を直接操作することもできます。

## Transform View

GUI の `Transform View` は、ビューポートと同じドックレイアウトで使える変形ワークスペースです。
`頂点` タブを開くとリグワークスペースと頂点選択へ自動で切り替わります。ビューポートまたは
Transform View 内の `選択`、`移動 G`、`回転 R`、`拡縮 S` を使って編集し、
`頂点モーフを作成` でPMXへ反映します。

`ボーン` タブもリグワークスペースへ自動で切り替わり、ボーン表示とボーン選択を有効にします。
一度に操作できるボーンは1本ですが、複数のボーンを順番に変形して1つのボーンモーフにできます。
IK、付与、物理などに制御されるボーンは操作できない理由を画面に表示します。

頂点とボーンの編集は独立した一時変形として保持され、タブとビューポートに変更数が表示されます。
一時変形はモーフを作成するまでPMX本体に含まれません。各タブの破棄ボタンはその種類だけを破棄し、
すべてを破棄する操作では確認画面を表示します。頂点の一時変形中は、元の基準位置を薄い点と線で確認できます。

`モーフ` タブではモーフミキサーからグループモーフの作成や頂点モーフへのベイクができます。
拡縮、反転、結合、減算、左右分割、材質マスクは `高度なモーフ操作` 内にまとめられています。
各PMX変更はUndo 1回で戻せます。

## CLI

```text
pmxer [edit-options] [file.pmx ...]
pmxer <command> [options] ...

pmxer info [--json] model.pmx
pmxer validate [--json] model.pmx [other.pmx ...]
pmxer diff [--json] [--profile logical|preservation] before.pmx after.pmx
pmxer normalize [-o output.pmx] model.pmx
pmxer help [command]
```

GUI起動時は `--font`、`--font-size`、`--resource-dir`、`--renderer`、`--gpu-debug`、
`--no-physics`、`--safe-mode` を指定できます。`PMXER_FONT` と
`PMXER_RESOURCE_DIR` 環境変数も利用できます。

## ログ

診断ログは DEBUG と INFO を標準出力、WARN と ERROR を標準エラーへ出力します。終了コードは、成功 0、検査結果の不合格 1、引数エラー 2、入出力エラー 3、内部エラー 70 です。

## ライセンス

本体のライセンスと依存ライブラリのライセンスは配布物に同梱します。
