# CHECK NODE

CHECK NODE는 **client watch program**으로, master PC가 slave PC들에 대하여 특정 URL과
프로그램에 접근하는 것을 차단시키는 프로그램이다.

> 팀 개발 시작점: [AccessBlock 팀 공유 가이드](docs/specs/README.md). 신규 구현자는
> `docs/team/P1~P3`보다 이 가이드와 `docs/specs/*_SPEC.md`를 우선한다.


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

아래 표는 제품 목표다. 현재 구현의 실제 보장과 차기 구조는 "구현 상태" 및
[`docs/specs/`](docs/specs/)를 기준으로 판단한다.

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

현재 legacy 구현은 master 툴바의 모드형 버튼(**Block access** / **Unblock access**)과
다음 커맨드라인을 사용한다:

    veyon-cli feature start <host> AccessBlock
    veyon-cli feature stop  <host> AccessBlock

이 Mode 기반 조작은 다른 Veyon mode와 배타적으로 동작하므로 장기 정책의 목표 계약이
아니다. 차기 v2는 독립적인 `Apply/Clear/Status` 명령과 장치별 적용 결과를 사용한다.


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
- 현재 구현은 **master → per-session server**로 목록을 보내고 server 플러그인이 WFP,
  브라우저 정책 및 프로세스 감시를 직접 적용한다. AccessBlock worker handler는 사용하지
  않는다.
- Windows는 WTS 세션별 server를 만들기 때문에 머신 전역 writer가 중복될 수 있다.
  차기 규범 구조는 **master → server 인증 gateway → machine-singleton service broker →
  Network/Process enforcer**다.
- GAN 검토, 전체 구조, 프로세스·웹 상세 명세는
  [`docs/specs/`](docs/specs/)에서 확인한다.


## 구현 상태

| 항목 | 상태 |
| --- | --- |
| master → server 메시지 배관 | 정적 경로 확인: Start/Stop 대상 목록이 서버 handler에 도달한다. 실제 OS 적용이나 Windows 종단간 성공 검증은 아님 |
| 웹사이트 접근 제어 로직 | Chrome/Edge policy와 DNS 결과 IP 기반 WFP의 best-effort 구현 존재. 외부 policy 소유권, 원자 갱신, ACK/복구 보강 전 운영 배포 불가 |
| 프로그램 실행 제어 로직 | basename 1초 polling 후 강제 종료하는 containment 구현 존재. 실행 예방이 아니며 identity/오탐/ACK 보강 필요 |
| 정책 상태·복구 | revision, 장치별 적용 ACK, journal, stale 명령 거부, boot reconcile 미구현 |
| Windows 검증 | 이전 정적 검토 외 Windows/MSYS2 compile과 실제 runtime 시험 미완료 |
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
