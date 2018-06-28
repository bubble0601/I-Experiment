# I実験
## ファイル
### 本体
- phone.c main関数
- lib.c 関数たち
- fft.c FFT
### ツール
- Makefile makeする
- read_data.c 入力(バイナリ)をgnuplotが認識できる形式にして出力
- a.plt read_dataで吐き出されたデータをグラフにプロット
### その他
試行錯誤したやつ

## 実行の流れ
options_t oを使って設定等の保存や受け渡しを行う(定義はlib.c)

parseでコマンドライン引数をoptions_tに保存する

その後非同期で録音->sendやrecv->再生する(phone関数)

## やったこと
- マルチスレッド(buffer overrunしなくなった)
- popenを使わずにlibsoxを使う(recやplayとのやりとりが不要な分速くなってるはず)
- qを入力してEnterを押すと終了(すぐに終了せずにフラグを立ててclose等の終了処理を行ってから終了する)(lib.rawterm.cを使うとEnterも不要になる)
- udpは体感では変わらなかった. 相手に通信終了を伝える処理は実装していない

## 使用例
./phone 10000 -p
./phone 127.0.0.1 10000 > /dev/null
