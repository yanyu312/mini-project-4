# MMSP 2025 — Mini Project 4  
Huffman Encoder / Decoder with Logging & CI

## 專案簡介
以 **Huffman 編碼**壓縮文字並解碼還原，輸出：
- `codebook.csv`（5 欄：symbol, count, probability, codeword, self-information；CSV 規格採 RFC 4180）  
- `encoded.bin`（位元串，二進位檔）  
- `output.txt`（解碼文字）  
- `encoder.log` / `decoder.log`（含 `start / summary / finish`）

> CSV 規範與 `text/csv` MIME：RFC 4180（IETF） 。:contentReference[oaicite:0]{index=0}

---

## 成員與分工
| 學號 | 姓名 | 分工內容 | 比例 |
| 411186038 | 陳彥妤 | Encoder/Decoder、VS Code 任務、CI | 34％ |
| 411286036 | 李後諺 | 撰寫以及push與simple相關的所有檔案 | 33％ |
| 411286016 | 楊昇儒 | 撰寫以及push與complex相關的所有檔案 | 33％ |

---

## 專案結構
.
├─ encoder.c
├─ decoder.c
├─ logger.c
├─ logger.h
├─ test_input_simple.txt # "Do you regret study communication Engineering?"
├─ .github/workflows/
│ ├─ c_build-simple.yml # simple：本地小檔驗證
│ └─ c_build-complex.yml # complex：curl 下載 cano.txt 後驗證
└─ .vscode/ (選用：本地任務)
├─ tasks.json
└─ launch.json

---

## 本機執行
```bat
:: 建置
gcc -std=c11 -O2 -Wall -Wextra -o encoder.exe encoder.c
gcc -std=c11 -O2 -Wall -Wextra -o decoder.exe decoder.c

:: Simple
.\encoder.exe test_input_simple.txt  test_codebook-simple.csv  test_encoded-simple.bin  > test_encoder-simple.log 2>&1
.\decoder.exe test_output-simple.txt test_codebook-simple.csv  test_encoded-simple.bin  > test_decoder-simple.log 2>&1
fc.exe /n test_input_simple.txt test_output-simple.txt  :: Windows 內建檔案比較工具 :contentReference[oaicite:1]{index=1}

:: Complex
curl -L -o test_input_complex.txt https://sherlock-holm.es/stories/plain-text/cano.txt
.\encoder.exe test_input_complex.txt  test_codebook-complex.csv  test_encoded-complex.bin  > test_encoder-complex.log 2>&1
.\decoder.exe test_output-complex.txt test_codebook-complex.csv  test_encoded-complex.bin  > test_decoder-complex.log 2>&1
fc.exe /n test_input_complex.txt test_output-complex.txt
