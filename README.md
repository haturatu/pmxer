# pmxer

pmxer は PMX 形式の編集、検証、保存、プレビューを行うクロスプラットフォームのデスクトップアプリケーションです。

## 方針

- PMX の参照を安定ハンドルで扱い、UI から生の添字を変更しない
- 保存前に検証し、一時ファイルへの保存と再読み込み比較を通過した場合だけ原子置換する
- 構造編集はトランザクションとして一度に確定する
- 編集履歴、診断、参照調査、意味差分を同じ編集層から提供する
- Linux、Windows、macOS のネイティブビルドを同じソースから作る

## 必要環境

- CMake 3.25 以上
- Ninja
- C++20 対応コンパイラ
- SDL3 開発ファイル（GUI ビルド時）
- ネットワーク接続（GUI の初回ビルドで Dear ImGui を取得する場合）

## ビルド

```sh
git clone --recursive <repository-url> pmxer
cd pmxer
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev
```

CLI は GUI なしでもビルドできます。

```sh
cmake --preset linux-cli
cmake --build --preset linux-cli
./build/linux-cli/pmxer-cli info model.pmx
```

## CLI

```text
pmxer-cli validate model.pmx
pmxer-cli info model.pmx
pmxer-cli diff before.pmx after.pmx
pmxer-cli normalize model.pmx output.pmx
```

## ログ

診断ログは DEBUG と INFO を標準出力、WARN と ERROR を標準エラーへ出力します。致命的な CLI エラーは終了コード 1 です。

## ライセンス

本体のライセンスと依存ライブラリのライセンスは配布物に同梱します。

