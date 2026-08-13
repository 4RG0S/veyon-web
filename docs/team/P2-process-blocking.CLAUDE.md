# CHECK NODE — 담당: 프로세스/프로그램 차단 엔진 (P2)

> 이 파일을 작업 디렉터리에 `CLAUDE.md`로 복사(Claude Code)하거나, GPT 프로젝트 컨텍스트로 붙여넣어 사용하세요.

## 프로젝트 개요

CHECK NODE는 Veyon(오픈소스 교실 관리 도구) 포크로, **master PC가 slave PC들의 특정 URL·프로그램 접근을 차단**하는 사내 PC 관리 도구다. master/slave 전부 Windows.

- 저장소: Veyon 4.11.0 포크
- 빌드: MSYS2 MINGW64 네이티브, Qt6, Ninja
- 현재 상태: `plugins/accessblock/` 기능 플러그인의 **master→server 메시지 배관은 완성·검증됨**. 실제 차단 로직은 미구현 → **그게 우리 팀 작업**.
- 화면 캡처/원격 보기는 **이번 범위 밖**.

## 네 역할

**slave에서 수신한 차단 프로그램 목록을 실제로 막는 엔진.**

- 입력: `BlockedApps` (QStringList) — 예: `["notepad.exe", "chrome.exe"]`
- 동작: 해당 프로그램의 slave에서의 실행 차단. 빈 리스트면 해제.

### 구현 방식 (권장)

1. **1순위 — 프로세스 감시 + 종료**
   - `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` + `Process32First/Next` 로 실행 중 프로세스 열거 (`tlhelp32.h`)
   - 차단 목록에 매칭되면 `OpenProcess(PROCESS_TERMINATE)` → `TerminateProcess`
   - `QTimer`로 주기 폴링(예: 1초). 활성 차단 목록을 멤버로 들고 계속 감시.
   - MinGW 헤더 지원 OK (`tlhelp32.h`, `processthreadsapi.h`, `handleapi.h`).
2. **보조/대안 — IFEO 레지스트리**
   - `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\<exe>` 에 `Debugger` 값 지정 → 실행 자체를 가로챔. 재부팅에도 유지되지만 되돌림 관리 필요.
   - 폴링 없이 막지만, 이미 떠 있는 프로세스는 못 죽임 → 1순위와 병행 고려.
- WMI 프로세스 생성 이벤트 구독은 MinGW 헤더 지원이 약해 **별도 검토 필요** — 우선 폴링으로 시작 권장.

### 소유 파일 (새로 생성)

- `plugins/accessblock/ProcessBlocker.h`
- `plugins/accessblock/ProcessBlocker.cpp`

클래스 인터페이스 (P3와 합의된 계약):
```cpp
class ProcessBlocker : public QObject
{
    Q_OBJECT
public:
    void apply( const QStringList& apps );  // 차단 시작 (감시 목록 교체)
    void clear();                            // 감시 중단 + 해제
private:
    // QTimer 폴링 등 내부 구현
};
```
P3가 `handleFeatureMessage(server)`에서 `m_processBlocker.apply(apps)` / `.clear()`를 호출한다. **`AccessBlockFeaturePlugin.cpp`는 P3 소유 — 직접 수정하지 말고 훅 지점만 협의.**

- `plugins/accessblock/CMakeLists.txt`의 `SOURCES`에 `ProcessBlocker.cpp ProcessBlocker.h` 추가 (P3와 협의).

## 인터페이스 계약 (전 팀 공통)

메시지 인자 enum (이미 정의됨, 변경 금지):
```cpp
enum class Argument { BlockedUrls, BlockedApps };
```
서버 수신부 라우팅 (P3 담당):
```cpp
const auto apps = message.argument(Argument::BlockedApps).toStringList();
const bool active = !urls.isEmpty() || !apps.isEmpty();
active ? m_processBlocker.apply(apps) : m_processBlocker.clear();
```

## 빌드 & 테스트

```bash
cmake -S . -B build -G Ninja -DWITH_BUNDLED_LIBVNC=ON -DWITH_LTO=OFF -DWITH_TRANSLATIONS=OFF
cmake --build build
./deploy.sh
```
GUI 없이 **CLI로 단독 테스트** 가능:
```bash
# 관리자/SYSTEM 권한 필요 (타 세션 프로세스 종료)
./run/veyon-server.exe
./run/veyon-cli.exe feature start localhost AccessBlock '{"blockedApps":["notepad.exe"]}'
# 인자 JSON 키는 enum 이름의 camelCase (argToString): blockedUrls / blockedApps
```
→ notepad 실행해서 즉시 종료되는지 확인.

## 반드시 지킬 코딩 제약 (Veyon 빌드 플래그)

- `-DQT_NO_KEYWORDS`: `emit`/`signals`/`slots` 금지 → `Q_EMIT`/`Q_SIGNALS`/`Q_SLOTS`
- `-DQT_NO_CAST_FROM_ASCII`: `QString s = "x"` 금지 → `QStringLiteral("x")`
- `-fno-exceptions`: 예외 금지 (Win32 반환값으로 에러 처리)
- C++20, 들여쓰기 **탭**, 파일 상단 GPL-2.0 헤더

## 금지

- `core/` 수정 금지
- `plugins/CMakeLists.txt` 수정 금지
- `AccessBlockFeaturePlugin.cpp/.h` 직접 수정 금지 (P3 소유)
- 이모지 금지

## 참고

- **권한**: 다른 사용자 세션의 프로세스를 죽이려면 서비스(SYSTEM) 권한 필요. 수동으로 일반 권한 실행 시 `TerminateProcess`가 `1314`(권한 부족)로 실패할 수 있다.
- 상태 복원: 재부팅/서버 재시작 시 감시 재개를 P3와 협의 (설정 목록을 시작 시 재적용).
- 화이트리스트 주의: 시스템 필수 프로세스를 실수로 못 죽이게 매칭을 정확히 (이름 완전일치 우선).
