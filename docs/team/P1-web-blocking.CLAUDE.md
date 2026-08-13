# CHECK NODE — 담당: 웹/URL 차단 엔진 (P1)

> 이 파일을 작업 디렉터리에 `CLAUDE.md`로 복사(Claude Code)하거나, GPT 프로젝트 컨텍스트로 붙여넣어 사용하세요.

## 프로젝트 개요

CHECK NODE는 Veyon(오픈소스 교실 관리 도구) 포크로, **master PC가 slave PC들의 특정 URL·프로그램 접근을 차단**하는 사내 PC 관리 도구다. master/slave 전부 Windows.

- 저장소: Veyon 4.11.0 포크
- 빌드: MSYS2 MINGW64 네이티브, Qt6, Ninja
- 현재 상태: `plugins/accessblock/` 기능 플러그인의 **master→server 메시지 배관은 완성·검증됨**. 실제 차단 로직은 미구현 → **그게 우리 팀 작업**.
- 화면 캡처/원격 보기는 **이번 범위 밖** (실제 VNC 서버 없음).

## 네 역할

**slave에서 수신한 차단 URL 목록을 실제로 적용/해제하는 엔진.**

- 입력: `BlockedUrls` (QStringList) — 예: `["example.com", "youtube.com"]`
- 동작: 해당 URL들을 slave의 브라우저에서 접근 차단. 빈 리스트면 차단 해제.

### 구현 방식 (권장)

1. **1순위 — 브라우저 정책 레지스트리 (`URLBlocklist`)**
   - Chrome: `HKLM\SOFTWARE\Policies\Google\Chrome\URLBlocklist\1..N` (REG_SZ, 값 = URL 패턴)
   - Edge: `HKLM\SOFTWARE\Policies\Microsoft\Edge\URLBlocklist\1..N`
   - 가장 안정적이고 브라우저가 즉시 반영. 해제는 키 삭제.
   - `RegSetValueEx`/`RegDeleteKey` (advapi32) — MinGW 헤더 지원 OK.
2. **보조 — hosts 파일** (`C:\Windows\System32\drivers\etc\hosts`)
   - 도메인 → `127.0.0.1` 매핑. 브라우저 무관하게 막지만 IP 직접접속·HTTPS 우회에 약함.
   - CHECK NODE 전용 마커 주석으로 우리 항목만 추가/제거하도록.

### 소유 파일 (새로 생성)

- `plugins/accessblock/UrlBlocker.h`
- `plugins/accessblock/UrlBlocker.cpp`

클래스 인터페이스 (P3와 합의된 계약):
```cpp
class UrlBlocker
{
public:
    void apply( const QStringList& urls );  // 차단 적용 (기존 것 교체)
    void clear();                            // 전체 해제
};
```
P3가 `handleFeatureMessage(server)`에서 `m_urlBlocker.apply(urls)` / `.clear()`를 호출한다. 너는 이 두 함수만 채우면 된다. **`AccessBlockFeaturePlugin.cpp`는 P3 소유 — 직접 수정하지 말고 P3와 훅 지점만 협의.**

- `plugins/accessblock/CMakeLists.txt`의 `SOURCES`에 `UrlBlocker.cpp UrlBlocker.h` 추가 (P3가 관리하는 파일이지만 이 줄 추가는 협의).

## 인터페이스 계약 (전 팀 공통)

메시지 인자 enum (이미 정의됨, 변경 금지):
```cpp
enum class Argument { BlockedUrls, BlockedApps };
```
서버 수신부는 이렇게 라우팅된다 (P3 담당):
```cpp
const auto urls = message.argument(Argument::BlockedUrls).toStringList();
const bool active = !urls.isEmpty() || !apps.isEmpty();
active ? m_urlBlocker.apply(urls) : m_urlBlocker.clear();
```

## 빌드 & 테스트

```bash
cmake -S . -B build -G Ninja -DWITH_BUNDLED_LIBVNC=ON -DWITH_LTO=OFF -DWITH_TRANSLATIONS=OFF
cmake --build build
./deploy.sh            # run/ 에 exe+dll 모음
```
GUI 없이 **CLI로 단독 테스트** 가능:
```bash
# 관리자 권한 필요 (HKLM 레지스트리 쓰기)
./run/veyon-server.exe          # slave 역할
./run/veyon-cli.exe feature start localhost AccessBlock '{"blockedUrls":["example.com"]}'
# 인자 JSON 키는 enum 이름의 camelCase (argToString): blockedUrls / blockedApps
```
→ 서버 로그에 차단 적용 확인 + 실제 Chrome에서 example.com 막히는지 확인.

## 반드시 지킬 코딩 제약 (Veyon 빌드 플래그)

- `-DQT_NO_KEYWORDS`: `emit`/`signals`/`slots` 금지 → `Q_EMIT`/`Q_SIGNALS`/`Q_SLOTS`
- `-DQT_NO_CAST_FROM_ASCII`: `QString s = "x"` 금지 → `QStringLiteral("x")`
- `-fno-exceptions`: 예외 금지 (Win32 반환값으로 에러 처리)
- C++20, 들여쓰기 **탭**, 파일 상단 GPL-2.0 헤더 (기존 플러그인 형식 따라)

## 금지

- `core/` 수정 금지 (전체 재빌드 + 리베이스 충돌)
- `plugins/CMakeLists.txt` 수정 금지 (glob 자동 등록)
- `AccessBlockFeaturePlugin.cpp/.h` 직접 수정 금지 (P3 소유 — 훅만 협의)
- 이모지 금지

## 참고

- 레지스트리/권한: 실제 적용은 **관리자 권한** 또는 서비스(SYSTEM) 필요. 비관리자면 HKLM 쓰기가 조용히 실패한다.
- 상태 복원: slave 재부팅/서버 재시작 시 차단 상태를 어떻게 유지/복원할지 P3와 협의 (설정에 저장된 목록을 서버 시작 시 재적용).
