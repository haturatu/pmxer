# CLI からモデルを調査・編集する

`inspect`、`query`、`plan`、`apply`、`render` は GUI を起動せずに利用できます。
編集エンジンは `pmxer_edit_engine`、Codex 通信は GUI の `pmxer_codex` に分離しています。
GUI と CLI は同じ面選択、編集操作、プレビュー、トランザクション、保存処理を使います。
CLI の利用に Codex 認証は不要です。

## モデルを調べる

```sh
pmxer inspect model.pmx materials --json
pmxer query model.pmx materials --name '*髪*' --json
pmxer query model.pmx vertices --material 'スカート*' --include-bones --json
pmxer inspect model.pmx bone --name '左ひじ' --include-weights --json
pmxer query model.pmx vertices --bone '左腕' --min -10 0 -10 --max 10 20 10 --json
pmxer inspect model.pmx morphs --offset 100 --limit 100 --json
```

対象は `materials`、`bones`（`bone` も使用可能）、`morphs`、`vertices` です。
名前は日本語名と英語名を検索し、`*` が任意の文字列に一致します。
頂点は材質、影響ボーン、モデル座標の範囲で絞り込みます。
`--include-weights` は頂点ではウェイト値、ボーンでは影響頂点数とウェイト合計を返します。
個々の影響頂点は `query ... vertices --bone NAME --include-weights` で取得します。

既定では 100 件、最大 1,000 件を返します。`total` と `next_offset` を使ってページを取得してください。
検索結果の `source_sha256` を編集前条件へ渡すと、ファイルの取り違えを検出できます。
ファイルから開いた CLI 文書の初期リビジョンは `0` です。GUI のリビジョンは文書の変更ごとに増加します。

## 編集計画を準備する

以下を `edit.json` に保存します。

```json
{
  "schema_version": 1,
  "selection": {
    "type": "material",
    "names": ["スカート"],
    "match": "exact",
    "expected_count": 1
  },
  "operations": [
    {"type": "transform", "scale": [1, 0.85, 1], "pivot": "top-center"},
    {"type": "smooth", "strength": 0.2}
  ],
  "guards": {
    "preserve_unselected": true,
    "preserve_uv": true,
    "preserve_weights": true,
    "preserve_morphs": true
  }
}
```

`selection` は材質全体を選びます。`match` は `exact`（既定）または `glob` です。
名前が重複する場合は、`names` の代わりに `indices: [0]` のように明示的な材質インデックスを指定できます。
その場合は `match` を省略し、調査結果の SHA-256 を前提条件に指定してください。
`expected_count` は必須です。一致件数が異なる場合は候補と件数を含む
`selection_count_mismatch` を返し、処理を中止します。

`preconditions` に `sha256` または `revision` を追加できます。
SHA-256 は小文字の 16 進数 64 桁、リビジョンは非負整数です。
未知のフィールド、対応していない操作、範囲外のパラメータはエラーになります。
計画は 64 KiB 以下、操作は 1～8 個です。

| 操作 | 主なパラメータ |
| --- | --- |
| `transform` | `translation`、`scale`、`rotation`（度）、`pivot` |
| `recolor` | `color`（RGBA、各値 0～1） |
| `smooth` | `strength` または `amount`（0～1）、`iterations`（1～3） |
| `inflate` | `amount`（PMX 単位、-10～10）、`iterations` |
| `subdivide` | `iterations` |
| `generate` | `shape`、`replace`、`detail`、`garment` |

`pivot` は `center`、`top-center`、`bottom-center` または選択範囲内の相対座標 `[x,y,z]`（0～1）です。
生成形状や衣装のパラメータは既存の GUI と共通です。`generate` は最初の操作に限ります。
完全なフィールドと数値範囲は次のコマンドで取得できます。

```sh
pmxer plan --schema --json > edit-plan.schema.json
```

## 試行して保存する

```sh
pmxer plan model.pmx --file edit.json --json
pmxer apply model.pmx --plan edit.json --output edited.pmx --dry-run --json
pmxer apply model.pmx --plan edit.json --output edited.pmx --json
pmxer validate edited.pmx --json
pmxer diff model.pmx edited.pmx --json
```

`plan` と `apply --dry-run` は同じプレビュー処理を実行し、保存しません。
結果には選択対象、編集前後の頂点数、移動頂点数、最大変位を含みます。
一連の操作は私有の文書で試行され、成功した場合だけ一つの Undo エントリーとして確定します。
途中の失敗で元文書が変更されることはありません。

ガードは省略時もすべて有効です。選択外の頂点や材質、UV、ウェイト、モーフの保持を検証します。
選択材質と未選択材質が頂点を共有する場合、その境界頂点の変更も拒否します。
トポロジー変更では `preserve_unselected`、`preserve_uv`、`preserve_weights` を
明示的に `false` にしてください。既存モーフの変更が必要なら `preserve_morphs` も無効にします。
ガードを無効にしても PMX の整合性検証は行います。

入力ファイルと同じ保存先は、シンボリックリンクやハードリンクを含めて拒否します。
保存先に既存ファイルがある場合は、検証付き保存の成功後に置き換えます。
保存は一時ファイル、再読み込み、意味比較、原子的な置き換えの順に行います。

`--json` の標準出力は JSON オブジェクト一つです。エラーは `error.code`、`error.message`、
必要に応じて `error.details` に格納され、診断ログは標準エラーにも出力されます。
終了コードは成功 `0`、モデル検証不合格 `1`、計画・前提条件エラー `2`、入出力・実行失敗 `3` です。

## 画像を生成する

```sh
pmxer render edited.pmx --views front,back,left,right --width 1024 --height 1024 --output-dir preview --json
pmxer render edited.pmx --overlay wireframe --output-dir wireframe
pmxer render edited.pmx --overlay 'weights:左腕' --output-dir weights
pmxer render edited.pmx --overlay bones --output-dir bones
pmxer render edited.pmx --morph '笑い=1.0' --motion motion.vmd --frame 120 --output-dir posed
```

CPU のソフトウェア描画で、選択した方向の PNG を生成します。SDL3、GPU、ディスプレイサーバーは不要です。
画像は共通の縮尺で中央に配置され、寸法は 16～4,096 ピクセルです。
PMX の座標に従い、正面は -Z、背面は +Z、左は +X、右は -X から見ます。
モーフと VMD は既存のプレビュー評価処理を使い、物理シミュレーションは無効にします。

このバックエンドは材質の diffuse 色、法線による簡易照明、深度を描画します。テクスチャ、透過合成、MMD の陰影を再現するものではありません。
ボーンのオーバーレイはレスト位置を表示します。形状、シルエット、ウェイトの確認に利用してください。

`PMXER_ENABLE_SOFTWARE_RENDERER=OFF` で画像バックエンドを省略できます。
その構成での `render` は `unsupported_backend` を返します。検索と編集計画は利用できます。

## 共通 API

- `editing::findComponents` / `editing::inspect`: 検索とページング付きの詳細取得。
- `editing::materialFaces`: 材質から面へ解決。GUI の選択キャプチャでも使用。
- `editing::prepareEdit`: 前提条件、選択、計画、プレビュー、ガードを検証。
- `ai::acceptEditPreview`: リビジョンと文書ドメインを確認し、一括確定。
- `saveDocument`: 保存後の意味比較を含む保存処理。

各 API はウィンドウや Codex 通信を必要としません。
MCP 公開と GUI セッション同期は別の実装対象です。
