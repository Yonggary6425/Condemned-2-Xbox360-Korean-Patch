# Condemned 2: Bloodshot (Xbox 360) 한글패치

> ## ⚠️ 이 패치는 PS3 한글패치를 Xbox 360판으로 **단순 포팅**한 것입니다
>
> 새로 번역한 것이 아닙니다. **번역문과 한글 폰트는 모두 PS3판 「컨뎀드 2: 블러드샷」 한글패치 원 제작자 SCPH1000님의 작업물**이며, SCPH1000님의 허락을 받아 Xbox 360판으로 옮겨 배포합니다.
>
> - **원본 PS3 한글패치 (SCPH1000님):** https://cafe.naver.com/hansicgu/34493
> - 레트로디비 안내 페이지: https://retrodb.info/krpatch/2303
>
> 번역에 대한 감사와 평가는 SCPH1000님께 돌려 주세요.
> 360판 적용 문제(실행 오류, 글자 깨짐 등)는 SCPH1000님이 아니라 이 저장소의 [Issues](../../issues)로 알려 주세요.

## 이식하면서 바꾼 것

번역 내용은 그대로 두고, 360판에서 동작하는 데 필요한 부분만 손봤습니다.

- 버튼 표기를 Xbox 360 기준으로 변경했습니다. 예: L1→LT, ×→A, L3→왼쪽 스틱. 바뀐 버튼 이름에 맞게 조사도 고쳤습니다.
- PS3판에서 둘로 나뉜 대사 4줄을 360판 구조에 맞게 합쳤습니다.
- PS3판에 없는 360 전용 문구 몇 줄을 번역했습니다. 예: Xbox LIVE, 게이머 프로필 관련 문구.

## 대상

| 항목 | 내용 |
|---|---|
| 게임 | Condemned 2: Bloodshot (USA, Europe) (En,Fr,Es,It) |
| 기종 | Xbox 360 (ISO) |
| 기반 | PS3 한글패치 Final v1.01 패키지 |

다른 지역판이나 이미 패치된 ISO는 패처가 자동으로 거부합니다.

## 사용 방법

1. [Releases](../../releases)에서 최신 버전을 받아 압축을 풉니다.
2. 원본 ISO 파일을 `Condemned2_360_KOR_Patcher.exe` 위로 끌어다 놓습니다.
   exe를 먼저 실행한 뒤 ISO를 창에 끌어다 놓고 Enter를 눌러도 됩니다.
3. 원본 ISO와 같은 폴더에 `원래이름 [KOR].iso`가 새로 만들어집니다. 원본 ISO는 수정하지 않습니다.

필요 사항
- Windows 10 이상 (64비트)
- 원본 ISO가 있는 드라이브에 약 8GB의 여유 공간

실행 환경
- **Xenia:** 새 ISO를 바로 실행합니다.
- **개조된 Xbox 360(RGH/JTAG):** ISO를 그대로 쓰거나, Iso2God 등으로 GOD 형식으로 바꿔 씁니다.

## 패처가 하는 일

- ISO에서 `Layer01.Arch02`의 문자열 DB 5개와 `interface\common.bndl`을 읽어 LZX 압축을 풉니다.
- 내장된 한국어 데이터를 적용한 뒤, 아카이브 끝에 덧붙이고 목차를 고칩니다.
- 이후 파일들의 디렉터리 정보를 옮겨 새 ISO를 만듭니다. 새 ISO는 원본보다 약 27MB 큽니다.
- 원본 목차와 데이터를 SHA-256으로 확인한 뒤에만 적용합니다.
- 다 만든 뒤에는 새 ISO를 다시 읽어 검증합니다.

패처에는 게임 데이터가 들어 있지 않습니다. 원본 데이터는 사용자의 ISO에서 읽고, 내장 데이터에는 바뀐 부분(한국어 문자열·한글 폰트)만 들어 있습니다.

## 소스에서 빌드

```sh
pip install ziglang
python3 -m ziglang cc -target x86_64-windows-gnu -O2 -municode \
  -DPATCH_BIN='"data/patch.bin"' -o Condemned2_360_KOR_Patcher.exe \
  src/c2kor.c src/lzx.c src/sha256.c src/blob.S
```

`data/patch.bin`은 패처에 내장되는 한국어 데이터입니다.

## 주의

- 비공식·비상업적 팬 패치입니다.
- 정품을 소유한 분만 이용해 주세요. 게임 ISO 파일을 배포하지 마세요.
- 실행파일 형태라 백신이 오진할 수 있습니다. 소스 코드는 이 저장소에 모두 공개되어 있습니다.

## 크레딧

- **한국어 번역 및 한글 폰트: SCPH1000님 (PS3 한글패치 원 제작자)** — https://cafe.naver.com/hansicgu/34493
- Xbox 360 이식(포팅): 용가리 ([Yonggary6425](https://github.com/Yonggary6425))
