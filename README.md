# BashBeats

BashBeats는 터미널에서 실행되는 C 기반 미니 DAW(Digital Audio
Workstation)입니다. `ncurses` 화면 안에서 트랙을 만들고, 피아노롤 방식으로
노트를 찍고, WAV 샘플 악기를 이용해 곡을 재생할 수 있습니다.

단순한 UI 데모가 아니라 다음 기능을 포함합니다.

- 터미널 기반 DAW 에디터
- 트랙 추가, 삭제, 음소거, 볼륨 조절
- 피아노롤 노트 입력 및 삭제
- BPM 변경과 재생 위치 이동
- WAV 샘플 기반 악기 재생
- 라이브 키보드 연주용 Performance Mode
- `.bbeat` 프로젝트 저장 및 불러오기
- WAV 파일 export
- TCP PCM 스트리밍

## 프로젝트 구조

```text
bashbeats/
├── include/      헤더 파일
├── src/          메인 프로그램, 에디터, 오디오 엔진 소스
├── samples/      기본 WAV 악기 샘플
├── saves/        예제 및 저장된 .bbeat 프로젝트
├── tools/        샘플 생성 등 보조 스크립트
├── docs/         상세 매뉴얼과 참고 문서
├── Makefile      빌드 스크립트
└── README.md
```

## 실행 환경

Ubuntu/Debian 기준으로 아래 패키지가 필요합니다.

```bash
sudo apt install build-essential libncurses-dev alsa-utils
```

`alsa-utils`는 로컬 오디오 재생에 사용하는 `aplay`를 제공합니다. `aplay`가
없어도 WAV export와 TCP 스트리밍은 동작하지만, 터미널에서 바로 소리를 들으려면
설치하는 것을 권장합니다.

## 빌드

기본 빌드는 실제 오디오 엔진을 포함합니다.

```bash
make
```

오디오 없이 UI 동작만 확인하고 싶다면 stub 빌드를 사용할 수 있습니다.

```bash
make stub
```

기본 샘플 WAV 파일을 다시 생성하려면 다음 명령을 사용합니다.

```bash
make samples
```

빌드 결과물을 지우려면 다음 명령을 사용합니다.

```bash
make clean
```

## 실행 방법

프로젝트 루트에서 실행합니다.

```bash
./bashbeats
```

저장된 프로젝트를 바로 열 수도 있습니다.

```bash
./bashbeats saves/full_band_demo.bbeat
```

처음 실행하면 인트로 화면에서 새 프로젝트, Performance Mode, 저장된 `.bbeat`
파일 중 하나를 선택할 수 있습니다.

## 빠른 사용법

### 인트로 화면

- `Up` / `Down`: 항목 이동
- `Enter`: 선택
- 텍스트 입력: 저장 파일 검색
- `Backspace`: 검색어 삭제
- `Ctrl+C`: 종료 확인

### TRACK 모드

TRACK 모드는 전체 트랙을 관리하는 화면입니다.

- `Enter`: 선택한 트랙을 EDIT 모드로 열기
- `Space`: 재생 / 일시정지
- `Up` / `Down`: 트랙 선택
- `Left` / `Right`: 재생 위치 이동
- `a`: 트랙 추가
- `d`: 트랙 삭제
- `i`: 악기 변경
- `b`: 기준 음 변경
- `m`: 음소거 토글
- `+` / `-`: 볼륨 조절
- `Esc`: 인트로 화면으로 돌아가기
- `Ctrl+F`: FILE 모드
- `Ctrl+C`: 종료 확인

### EDIT 모드

EDIT 모드는 피아노롤처럼 노트를 입력하는 화면입니다.

- `Arrow keys`: 커서 이동
- `Enter`: 현재 위치에 노트 입력 또는 토글
- `Delete`: 현재 위치의 노트 삭제
- `Space`: 현재 커서 위치부터 재생 / 일시정지
- `[` / `]`: 편집 트랙 이동
- `+` / `-`: BPM 조절
- `,` 다음 `.`: 긴 노트 입력 범위 지정
- `Esc`: TRACK 모드로 돌아가기
- `Ctrl+F`: FILE 모드
- `Ctrl+C`: 종료 확인

재생 중 방향키로 위치를 조정하면 재생이 일시정지되고, 현재 울리던 소리를 정리한
뒤 새 위치에 맞춰 다시 시작할 수 있습니다.

### FILE 모드

FILE 모드는 프로젝트 저장, 불러오기, 제목 변경, WAV export를 담당합니다.

- `S`: 저장
- `L`: 저장된 `.bbeat` 파일 불러오기
- `N`: 새 프로젝트 만들기
- `R`: 저장 경로 변경
- `T`: 프로젝트 제목 변경
- `E`: WAV 파일로 export
- `Esc`: TRACK 모드로 돌아가기
- `Ctrl+C`: 종료 확인

### Performance Mode

Performance Mode는 컴퓨터 키보드로 악기를 직접 연주하는 모드입니다.

- `z x c v b n m`: 낮은 옥타브 흰 건반
- `s d g h j`: 낮은 옥타브 검은 건반
- `q w e r t y u`: 높은 옥타브 흰 건반
- `2 3 5 6 7`: 높은 옥타브 검은 건반
- `Up` / `Down`: 옥타브 변경
- `i`: 악기 변경
- `Esc`: 인트로 화면으로 돌아가기
- `Ctrl+C`: 종료 확인

## 예제 프로젝트

`saves/` 폴더에는 테스트용 프로젝트와 데모 프로젝트가 들어 있습니다.

- `saves/full_band_demo.bbeat`: 모든 기본 악기를 포함한 데모 곡
- `saves/testver23.bbeat`: 기존 테스트 프로젝트
- `saves/testbashb.bbeat`: 기존 테스트 프로젝트
- `saves/Untitled.bbeat`: 기본 저장 예제

전체 악기 소리를 확인하려면 다음처럼 실행하면 됩니다.

```bash
make
./bashbeats saves/full_band_demo.bbeat
```

## 파일 형식

BashBeats는 자체 텍스트 기반 프로젝트 파일인 `.bbeat`를 사용합니다. 이 파일에는
프로젝트 제목, BPM, 트랙 목록, 악기 경로, 볼륨, 음소거 상태, 노트 이벤트가
저장됩니다.

샘플 악기는 16-bit PCM WAV 파일을 사용합니다. 기본 샘플은 `samples/` 폴더에
포함되어 있으며, `tools/generate_samples.py`로 다시 생성할 수 있습니다.

## 개발자

| 이름 | 담당 역할 |
|---|---|
| Jimin Bae | Core implementation: C application logic, editor features, audio integration, file I/O, and build setup |
| Changwoo Ha | Planning and design: project concept, user workflow, terminal UI direction, and feature prioritization |

The project was divided between implementation-focused work and
planning/design-focused work so that the technical build and user experience
could be developed together.

## 상세 매뉴얼

더 자세한 조작법은 [docs/MANUAL.md](docs/MANUAL.md)를 참고하세요.
