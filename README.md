# CHECK NODE

CHECK NODE는 **client watch program**으로, master PC가 slave PC들에 대하여 특정 URL과
프로그램에 접근하는 것을 차단시키는 프로그램이다.


## 개요

- **master PC** — 관리자가 사용. 차단 정책을 지정하고 slave에 적용/해제한다.
- **slave PC** — 관리 대상. master의 지시에 따라 접근 차단을 수행한다.

master와 slave는 네트워크로 연결되며, master가 각 slave의 에이전트(서버)에 접속해
제어 메시지를 전송하는 구조다.


## 요구사항

- **master PC와 slave PC는 전부 Windows여야 한다.**


## 수행하는 역할

- **웹사이트 접근 제어** — 지정한 URL에 대한 slave의 접근을 차단한다.
- **프로그램 실행 제어** — 지정한 프로그램의 slave에서의 실행을 차단한다.


## 기능 명세

### 1. 웹사이트 접근 제어

| 항목 | 내용 |
| --- | --- |
| 대상 지정 | master에서 차단할 URL 목록을 지정한다. |
| 적용 범위 | 선택한 slave PC(단일/다중)에 일괄 적용한다. |
| 동작 | slave에서 해당 URL에 대한 브라우저 접근을 차단한다. |
| 켜기/끄기 | master의 토글로 차단을 시작(Block)하고 해제(Unblock)한다. |
| 상태 | 차단 활성/비활성 상태가 slave별로 유지된다. |

### 2. 프로그램 실행 제어

| 항목 | 내용 |
| --- | --- |
| 대상 지정 | master에서 차단할 프로그램(실행 파일) 목록을 지정한다. |
| 적용 범위 | 선택한 slave PC(단일/다중)에 일괄 적용한다. |
| 동작 | slave에서 해당 프로그램의 실행을 차단한다. |
| 켜기/끄기 | master의 토글로 차단을 시작(Block)하고 해제(Unblock)한다. |
| 상태 | 차단 활성/비활성 상태가 slave별로 유지된다. |

### 조작 방식

차단 기능은 master 툴바의 모드형 버튼(**Block access** / **Unblock access**)으로
제공되며, 커맨드라인에서도 실행할 수 있다:

    veyon-cli feature start <host> AccessBlock
    veyon-cli feature stop  <host> AccessBlock


## Veyon 기반 기능

CHECK NODE는 Veyon을 기반으로 하므로 다음 기능들도 함께 제공된다:

- **모니터링(Overview)** — 여러 위치/컴퓨터를 한눈에 확인
- **원격 접근(Remote access)** — 컴퓨터 화면 보기 또는 원격 제어
- **데모(Demo)** — master 화면을 slave들에 실시간 브로드캐스트
- **화면 잠금(Screen lock)** — 입력 장치 잠금 및 화면 가리기
- **메시지(Communication)** — 텍스트 메시지 전송
- **로그인/로그아웃** — 사용자 일괄 로그인/로그아웃
- **스크린샷(Screenshots)** — 화면 캡처 기록
- **프로그램·웹사이트** — 원격으로 프로그램 실행 / 웹사이트 열기
- **자료 배포(Teaching material)** — 문서·이미지·영상 배포 및 열기
- **관리(Administration)** — 원격 전원 켜기/끄기, 재부팅

> 참고: 화면 캡처 기반 기능(모니터링 썸네일, 원격 화면 보기, 데모, 스크린샷)은
> 현재 Windows용 화면 캡처 VNC 서버가 제거된 상태라 정상 동작하지 않을 수 있다.
> 아래 "구현 상태"를 참고한다.


## 아키텍처

- 차단 기능은 `plugins/accessblock/` 의 **AccessBlock** 기능 플러그인으로 구현된다.
- 메시지 흐름은 **master → server → worker** 3단 구조를 따른다.
  master가 차단 대상(URL/프로그램 목록)을 담아 slave 서버로 전송하면,
  서버가 이를 해석해 차단을 적용한다.


## 구현 상태

| 항목 | 상태 |
| --- | --- |
| master → server 메시지 배관 | 구현 및 종단간 검증 완료 (Start/Stop이 대상 목록을 담아 서버 핸들러까지 도달) |
| 웹사이트 접근 제어 로직 | 미구현 (서버 핸들러가 수신 상태를 로그로만 출력) |
| 프로그램 실행 제어 로직 | 미구현 |
| slave 화면 보기(원격 화면 조회) | 현재 미포함 — Windows용 화면 캡처 VNC 서버가 제거된 상태 |


## 빌드

MSYS2 MINGW64 네이티브 환경(Qt6, Ninja):

    cmake -S . -B build -G Ninja -DWITH_BUNDLED_LIBVNC=ON -DWITH_LTO=OFF -DWITH_TRANSLATIONS=OFF
    cmake --build build

빌드 후 `./deploy.sh` 를 실행하면 `run/` 에 실행 파일과 플러그인 DLL이 모인다.


## 라이선스 및 기반

이 프로젝트는 [Veyon](https://veyon.io) 을 기반으로 하며, **GNU GPLv2** 라이선스를 따른다.
자세한 내용은 `COPYING` 파일을 참고한다.

Copyright (c) 2004-2026 Tobias Junghans / Veyon Solutions 및 CHECK NODE 기여자.
