# JavaChat

[English README is here (README.md)](README.md)

Minecraft Bedrock（統合版 / Windows x64）の重いチャット画面を、**本家 Java Edition に忠実な軽量チャットオーバーレイ＋ミニマル 1 行入力欄**へ置き換える **LeviLamina クライアント MOD** です。

> **対応・検証状況**: **LeviLamina 26.60.8**（Minecraft Bedrock **1.26.60.5**）向けにビルドされています。コアロジックの単体テスト（`jchat_tests`）および Minecraft 1.26.60.05 実機での描画・Esc 1 回閉じ・補完重なり回避・本家準拠レイアウトの動作確認済みです。

---

## なぜ軽くなるのか？（ハイブリッド構成）

統合版で `T` や `/` を押すたびに重くなる最大の理由は、バニラの `chat_screen.json` が毎回「大量の履歴スクロールリスト・ヘッダー・ホストメニュー・設定ポップアップ・（マルチサーバーでは独自サイドバー）」を再生成・レイアウト計算しているためです。

JavaChat では役割を明確に分離しています：
1. **超軽量 JSON-UI（同梱パック `JavaChatUI`）**: 重い履歴パネル・ヘッダー・サイドバー・統合版既定の補完リストを取り除き、**高さ 12px の本家 Java 風 1 行入力欄だけ**を残します。バニラ左上の HUD チャットも非表示化します（座標表示やペーパードールには影響しません）。
2. **C++ オーバーレイ描画（`MinecraftUIRenderContext` & `TextComponent`）**: `GuiData::mGuiMessages` からメッセージを差分同期し、折り返しキャッシュ（`Font::getLineLength`）付きで左下に描画します。さらに本家 Java Edition 準拠の**コマンド補完ポップアップ**（トークン名のみ・幅自動フィット・黄色選択 `#FFFF55`・↑/↓ やホイール・クリック・Tab 循環対応）、**入力欄の灰色インライン補完プレビュー**、**コマンド構文カラー表示**、**日本語 IME 変換の完全維持**を実現しています。

---

## 主な機能

- **本家 Java Edition に忠実な見た目と操作感**
  - 左下に純黒の半透明帯（既定 `backgroundOpacity: 0.50`、`lineSpacing: 0.0` で 9px 詰め表示）＋影付きテキスト。枠線や余計な装飾なし。
  - チャットを開いた状態・閉じた状態とも同じ下端位置（既定 `bottomOffset: 40.0`、ホットバーや体力バーの上）で揃い、補完ポップアップが開いてもチャット履歴は上下に跳ねず、補完ボックスが自然な Z 順で前面に表示されます。
  - **本家仕様の 12px 入力欄＆構文ハイライト**: `/` 入力時にコマンド構文カラー（有効コマンドは灰色、未完成・不正コマンドは赤 `#FF5555`、以降の引数は Java 版 Brigadier の 5 色ローテーション：水色/黄/緑/薄紫/金）、薄い灰色のインライン補完プレビュー、`_` / `|` カーソルに加え、**`Ctrl+A` 全選択編集**（選択ハイライト・`Ctrl+C`/`Ctrl+X`/`Ctrl+V`・`Backspace`/`Delete`・上書き入力＆日本語 IME 置換）に対応しています。
  - **本家仕様のコマンド補完ポップアップ**: `- 説明文` やアイコン余白を除いたトークン名のみを、入力中トークン直上に幅自動フィットで表示（選択中 `#FFFF55` 黄色・非選択 `#AAAAAA` 灰色）。補完表示中は `↑`/`↓`・`Tab`/`Shift+Tab`・マウスホバー・クリック・ホイールで操作でき、補完が閉じている時は `↑`/`↓` で送信履歴を辿れます。
  - チャットを開いても画面全体が暗転しません。
  - **ワールド・サーバー切り替え時の履歴クリア**: サーバーやワールドを移動した際に前の履歴を残さず自動でクリアします（`clearHistoryOnWorldSwitch`）。
  - **Esc キー 1 回で即閉じ**: 入力欄のフォーカス解除だけで止まらず、1 回の Esc でチャット画面を閉じられます。
  - **送信後に自動で閉じる**: チャットやコマンドを送信すると本家 Java Edition 同様に自動でチャット画面を閉じます（日本語 IME の変換確定 Enter では誤爆しません、`autoCloseOnSend`）。
  - **開閉アニメーション完全撤廃**: 画面のスライド／フェード遷移やローディングバーを無効化し、0ms で即時開閉します（`disableChatAnimations`）。
  - **チャット中も HUD・他 MOD の HUD を維持**: チャットを開いている間もバニラ HUD（ホットバー・座標・ペーパードール等）や他 MOD の HUD オーバーレイを表示したままにします（`keepHudVisibleInChat`）。
- **フォント変更（履歴＋入力欄＋補完欄連動）**:
  - Bedrock 内蔵フォント（`default` / `smooth` / `unicode` / `MinecraftTen` / `rune` / `SmoothSmall`）をコマンドで即座に切り替えられます。
  - 任意の `.ttf` ファイル（フルパス／`config/fonts/`／Windows フォント名）にも対応し、`.ttf` 側に未収録の文字（日本語など）がある行は自動で `default` フォントへフォールバックします（※新しい `.ttf` の反映にはゲーム再起動が必要です）。
- **履歴拡張と快適なスクロール**
  - 既定 **500 件**（最大 `5000` 件）まで履歴を保持。
  - **マウスホイール**（`Shift + ホイール` で 1 行ずつ）、**`PageUp` / `PageDown`**、**`Ctrl+Home` / `Ctrl+End`**、さらに**スクロールバーのクリック／ドラッグ**に対応。
  - 上へスクロールして履歴を読んでいる最中に新しいメッセージが届いても、見ていた行の位置がズレません（アンカー維持）。
  - Bedrock 固有カラーコード（`§m` / `§n` 等のマテリアルカラー含む）の折り返し行引き継ぎに対応。
- **あると嬉しい便利機能**
  - **重複まとめ（Compact Chat）**: 連続する同一メッセージを `[xN]` に集約（`compactDuplicates`）。
  - **タイムスタンプ**: `[HH:MM]` プレフィックス表示（`showTimestamps`）。
  - **クリック操作**（入力欄のフォーカスを外さずに実行）：
    - `http://` / `https://` を含む行を左クリック → ブラウザで開く（折り返された行ごとの URL に対応）。
    - 通常行を左クリック／任意の行を右クリック → `§` コードを除去した本文をクリップボードへコピー。
- **サーバー配布 UI の上書き**
  - `ResourcePackManager::composeFullStack` を `HookPriority::High` でフックし、`JavaChatUI` をスタック先頭（index 0）に自動固定。マルチサーバー独自の重いチャット UI があっても上書きできます（`overrideServerChatUi` で無効化可）。

---

## 導入方法

### 方法 1: `lip` / LeviLauncher から導入
リポジトリに [`lip.toml`](lip.toml)（format 4、現行 `lip`・LeviMC Mods 掲載対応）および互換用の [`tooth.json`](tooth.json) を同梱しているため、`lip` / LeviLauncher のパッケージ導入が使える環境では以下のコマンドでインストールできます：

```powershell
lip install github.com/sh-ayu/JavaChat
```

アップデート時も既存の `mods/JavaChat/config/config.json` は保持されます。

### 方法 2: リリース ZIP（`JavaChat-client-windows-x64.zip`）を手動導入
1. [Releases](https://github.com/sh-ayu/JavaChat/releases) から `JavaChat-client-windows-x64.zip` をダウンロードして展開し、`JavaChat` フォルダごと LeviLauncher インスタンスの `mods/` に配置します：
   ```text
   %LOCALAPPDATA%\LeviLauncher\Instances\<インスタンス名>\mods\JavaChat\
   ├── JavaChat.dll
   ├── manifest.json
   ├── config.sample.json
   ├── LICENSE
   ├── README.md
   ├── README_JA.md
   └── resource_packs\
       └── JavaChatUI\
   ```
2. LeviLauncher からゲームを起動します（`mods/JavaChat/resource_packs/JavaChatUI/` は LeviLamina が自動登録します）。
3. 初回起動時に設定ディレクトリへ `config/config.json` が自動生成されます（ログの `JavaChat loaded (config: ...)` に正確なパスが出ます）。

---

## クライアントコマンド（`/jchat`）

ゲーム内チャットからリアルタイムに設定を変更でき、自動で `config.json` に保存されます：

| コマンド | 説明 |
|---|---|
| `/jchat` または `/jchat help` | コマンド一覧を表示 |
| `/jchat status` | 現在の状態・履歴数・レイアウト設定・UI パック固定状態を表示 |
| `/jchat toggle` | JavaChat オーバーレイの ON / OFF を切り替え |
| `/jchat clear` | 蓄積されたチャット履歴をクリア |
| `/jchat reload` | `config.json` を再読み込み |
| `/jchat reset` | 全設定を初期値にリセット |
| `/jchat width <120..640>` | チャット欄の幅を変更 |
| `/jchat height <closed> <open>` | 閉じた時（`24..360`）・開いた時（`40..480`）の最大高さを変更 |
| `/jchat position <left> <bottom>` | 左端オフセット（`0..240`）と下端オフセット（`18..240`）を変更 |
| `/jchat scale <0.5..2.0>` | 文字サイズ倍率を変更 |
| `/jchat spacing <0..8>` | 行間（px）を変更 |
| `/jchat font` / `/jchat font list` | 現在のフォント・内蔵プリセット・`config/fonts/*.ttf` 一覧を表示 |
| `/jchat font <名前またはパス>` | 内蔵フォント（即時反映）または `.ttf` フォント（再起動で反映）へ変更 |
| `/jchat opacity <0..1>` | 背景の不透明度を変更 |
| `/jchat textopacity <0.1..1>` | 文字の不透明度を変更 |
| `/jchat fade <1..120>` | 閉じた状態でフェードアウトし始めるまでの秒数を変更 |
| `/jchat fade <delay> <duration>` | フェード開始秒数（`1..120`）とフェード時間（`0..10`秒、`0` で即消し）を変更 |
| `/jchat autoclose <true\|false>` | 送信後の自動クローズの ON / OFF |
| `/jchat animations <true\|false>` | チャット開閉アニメーションの ON / OFF（`false` が本家準拠） |
| `/jchat keephud <true\|false>` | チャットを開いている間のバニラ HUD・他 MOD HUD 表示の ON / OFF |
| `/jchat shadow <true\|false>` | 文字の影の ON / OFF |
| `/jchat compact <true\|false>` | 連続重複まとめ `[xN]` の ON / OFF |
| `/jchat timestamps <true\|false>` | `[HH:MM]` タイムスタンプの ON / OFF |
| `/jchat clickcopy <true\|false>` | クリックコピーの ON / OFF |
| `/jchat clickurl <true\|false>` | URL クリック起動の ON / OFF |
| `/jchat clearonswitch <true\|false>` | ワールド・サーバー切り替え時の履歴クリアの ON / OFF |

---

## 設定ファイル（`config.json`）

`//` 行コメント、`/* ... */` ブロックコメント、末尾カンマ（JSONC）に対応しています。範囲外の値は安全な範囲へ自動クランプされ、書式が壊れている場合は `config.json.corrupt-<時刻>` へバックアップしたうえでデフォルトを再生成します。

| キー | 既定値 | 説明 |
|---|---|---|
| `enabled` | `true` | MOD オーバーレイの有効/無効 |
| `overrideServerChatUi` | `true` | `JavaChatUI` をパックスタック最上位に固定 |
| `width` | `320.0` | チャット幅（GUI px、`120`〜`640`） |
| `closedHeight` | `90.0` | 閉じた状態の最大高さ（`24`〜`360`） |
| `openHeight` | `180.0` | 開いた状態の最大高さ（`40`〜`480`） |
| `bottomOffset` | `40.0` | 下端からの距離（開閉共通、`18`〜`240`） |
| `leftOffset` | `2.0` | 左端からの距離（`0`〜`240`） |
| `fontScale` | `1.0` | フォント倍率（`0.5`〜`2.0`） |
| `lineSpacing` | `0.0` | 行間（`0.0`〜`8.0`） |
| `backgroundOpacity` | `0.50` | メッセージ背景の不透明度（`0.0`〜`1.0`） |
| `textOpacity` | `1.0` | 文字の不透明度（`0.1`〜`1.0`） |
| `textShadow` | `true` | 文字の影を描画するか |
| `fadeDelaySec` | `9.0` | フェード開始までの表示時間（秒、`1`〜`120`） |
| `fadeDurationSec` | `1.0` | フェードアウトにかかる時間（秒、`0.0`〜`10`、`0` でフェードなし即消し） |
| `maxHistoryMessages` | `500` | 保持する履歴件数上限（`20`〜`5000`） |
| `scrollStepLines` | `3` | ホイール 1 ノッチあたりのスクロール行数（`1`〜`20`） |
| `compactDuplicates` | `true` | 連続する同一メッセージを `[xN]` にまとめる |
| `showTimestamps` | `false` | 先頭に `[HH:MM]` を表示 |
| `clickToCopy` | `true` | メッセージクリックでコピー |
| `clickToOpenUrl` | `true` | `http://` / `https://` リンクの左クリックでブラウザを開く |
| `autoCloseOnSend` | `true` | メッセージ／コマンド送信後にチャット画面を自動で閉じる |
| `disableChatAnimations` | `true` | チャット開閉時のスライド／フェードアニメーションを無効化 |
| `keepHudVisibleInChat` | `true` | チャットを開いている間も `hud_screen` や他 MOD の HUD を維持 |
| `font` | `"default"` | 内蔵フォント（`default`/`smooth`/`unicode`/`MinecraftTen`/`rune`/`SmoothSmall`）または `.ttf` パス／ファイル名 |
| `clearHistoryOnWorldSwitch` | `true` | ワールド・サーバー切り替え時に履歴を消去 |
| `verboseLog` | `false` | 詳細ログを出力 |

---

## ソースからのビルド手順

Windows x64、Visual Studio Build Tools、LLVM (`clang-cl`)、[xmake](https://xmake.io/) 3.0 以上が必要です。

```bat
:: 1. スタンドアロン単体テストの実行（LeviLamina 不要）
xmake f -m release --with_mod=n -y
xmake build jchat_tests
xmake run jchat_tests

:: 2. LeviLamina 26.60.8 クライアント MOD DLL＋UIパックのビルド
xmake f -m release --with_mod=y -y
xmake build JavaChat

:: 3. （任意）dist/ に配布用 ZIP を作成
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

---

## 安全性

- **完全クライアントサイド**: JavaChat はローカルの `GuiData::mGuiMessages` を読み取って描画するだけで、通信パケットの送信・偽造・改変は一切行いません。
- URL クリック機能は `http://` または `https://` スキームのみを許可し、それ以外のスキームは無視します。

---

## 既知の制限

- **Java 版の `hoverEvent` / `clickEvent`**: 統合版の通信プロトコル（`TextPacket`）にはホバー詳細やクリックコマンド情報が含まれないため、メッセージ内 URL の起動とクリップボードコピーのみ対応しています。
- **ライブアンロード非対応**: MOD の更新・削除時はゲームを再起動してください。
- **ゲーム中の `/jchat toggle` OFF**: C++ 側のオーバーレイは即座に非表示になりますが、バニラのチャット UI を復帰させるにはワールド再参加または再起動が必要です。
- **将来の OreUI 移行**: 将来のアップデートでチャット画面が OreUI へ完全移行された場合、`chat_screen.json` による入力欄カスタマイズは影響を受ける可能性があります。

---

## ライセンス

[MIT License](LICENSE) の下で公開されています。
