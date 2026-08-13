# CHECK NODE — 담당: 마스터 GUI / 설정 / 통합 / 빌드 (P3, 리드)

> 이 파일을 작업 디렉터리에 `CLAUDE.md`로 복사(Claude Code)하거나, GPT 프로젝트 컨텍스트로 붙여넣어 사용하세요.

## 프로젝트 개요

CHECK NODE는 Veyon(오픈소스 교실 관리 도구) 포크로, **master PC가 slave PC들의 특정 URL·프로그램 접근을 차단**하는 사내 PC 관리 도구다. master/slave 전부 Windows.

- 빌드: MSYS2 MINGW64 네이티브, Qt6, Ninja
- 현재 상태: `plugins/accessblock/` 의 **master→server 배관 완성·검증됨**. 실제 차단 로직 미구현.
- 화면 캡처/원격 보기는 **범위 밖**.
- 플러그인 식별자: IID `io.veyon.Veyon.Plugins.AccessBlock`, Plugin UID `4f8c8f5b-abe0-4d8d-a8f6-e69bcced74dd`, Feature UID `dbeee12f-b78b-42cd-be9a-aec1fbd7fb2f`.

## 네 역할 (통합자/리드)

1. **마스터 GUI/설정** — 차단할 URL·앱 목록을 관리하는 UI, 버튼 동작.
2. **메시지/설정 계약 관리** — P1(웹)·P2(프로세스)를 잇는 인터페이스 소유.
3. **통합** — `UrlBlocker`(P1)·`ProcessBlocker`(P2)를 서버 수신부에 연결.
4. **빌드/배포/테스트** — `deploy.sh`, `dist.sh`, 서비스 설치, 키 배포, 2대 테스트, 코드 리뷰.

### 소유 파일

- `plugins/accessblock/AccessBlockFeaturePlugin.{h,cpp}` — 플러그인 셸 + 메시지 라우팅
- `plugins/accessblock/AccessBlockConfiguration.h` — 설정 스키마 (blockedUrls, blockedApps)
- `plugins/accessblock/AccessBlockConfigurationPage.{h,cpp,ui}` — 설정 페이지 GUI
- `plugins/accessblock/CMakeLists.txt` — SOURCES 관리 (P1/P2 파일 추가 반영)

### GUI 작업

- `ConfigurationPagePluginInterface` 구현 → 설정 페이지에 URL 목록·앱 목록 `QListWidget` (추가/삭제/저장).
- Block access 클릭 시 대상/목록 적용 흐름 (필요하면 다이얼로그).
- **참고 구현: `plugins/desktopservices/`** — 설정 페이지 + `.ui` + 목록 관리 + 다이얼로그의 가장 가까운 예시.
- 모드 토글(on/off) 참고: `plugins/screenlock/`.

## 인터페이스 계약 (네가 소유·관리)

메시지 인자 enum (이미 정의됨):
```cpp
enum class Argument { BlockedUrls, BlockedApps };  // 정수값 0, 1
```

**controlFeature (master→server 전송, 네 담당):**
```cpp
// 설정에서 목록 읽어 전송
sendFeatureMessage(FeatureMessage{featureUid, FeatureMessage::Command::Default}
    .addArgument(Argument::BlockedUrls, blockedUrls)
    .addArgument(Argument::BlockedApps, blockedApps),
    computerControlInterfaces);
```

**handleFeatureMessage(server) (수신, 네가 P1/P2 훅 연결):**
```cpp
const auto urls = message.argument(Argument::BlockedUrls).toStringList();
const auto apps = message.argument(Argument::BlockedApps).toStringList();
const bool active = !urls.isEmpty() || !apps.isEmpty();
if (active) { m_urlBlocker.apply(urls); m_processBlocker.apply(apps); }
else        { m_urlBlocker.clear();     m_processBlocker.clear();     }
```
- `m_urlBlocker` (`UrlBlocker`) = **P1 소유**, 헤더만 include.
- `m_processBlocker` (`ProcessBlocker`) = **P2 소유**, 헤더만 include.
- 이 계약(클래스명·시그니처·enum)을 확정해 P1/P2에 공유하고, 변경 시 조율.

## 빌드 / 배포 / 테스트

```bash
# 새 파일(P1/P2가 추가) 반영하려면 configure 재실행
cmake -S . -B build -G Ninja -DWITH_BUNDLED_LIBVNC=ON -DWITH_LTO=OFF -DWITH_TRANSLATIONS=OFF
cmake --build build
./deploy.sh          # run/ 에 모음
bash dist.sh         # dist/ 에 MSYS2 없는 PC용 자립 번들 (Qt플러그인+DLL+QCA 번들)
```
배관/기능 테스트:
```bash
./run/veyon-server.exe          # slave
./run/veyon-cli.exe feature start localhost AccessBlock
./run/veyon-cli.exe feature stop  localhost AccessBlock
# 서버 로그: SERVER: AccessBlock active/inactive
```

### 배포·운영에서 이미 파악된 함정 (팀에 전파)

- **설정 저장은 관리자 권한 필요**: Veyon 설정은 `HKLM\Software\Veyon Solutions\Veyon`. 비관리자면 `config set`/`networkobjects add`가 `[OK]` 떠도 **저장 안 됨**. 반드시 관리자 cmd.
- **워커 필요 기능은 서비스(SYSTEM)로**: 잠금·메시지·앱실행 등은 `veyon-cli service register`로 설치해야 동작. 수동 실행 시 `CreateProcessAsUser 1314` 실패.
- **키 인증**: master에 개인키, slave에 **동일 키쌍의 공개키**. `authkeys list details`의 **PAIR ID**가 양쪽 동일해야 함. slave 공개키 접근그룹은 서버 실행 계정이 읽을 수 있게(`Users`).
- **VNC 서버**: 이 포크는 실제 화면 캡처 서버가 없어 headless(빈 화면)를 기본 사용. 관련 수정은 커밋 `93a0fb104`에 반영됨 — slave에서 별도 설정 불필요.
- **MSYS2 없는 PC 실행**: `dist/` 폴더째 복사. `dist.sh`가 Qt 플러그인·mingw DLL·QCA provider를 전부 번들.

## 반드시 지킬 코딩 제약

- `-DQT_NO_KEYWORDS` → `Q_EMIT`/`Q_SIGNALS`/`Q_SLOTS`
- `-DQT_NO_CAST_FROM_ASCII` → `QStringLiteral("x")`
- `-fno-exceptions`, C++20, 들여쓰기 **탭**, GPL-2.0 헤더

## 금지

- `core/` 수정 금지, `plugins/CMakeLists.txt` 수정 금지, 이모지 금지
- `dist/`, `run/`, `build/` 커밋 금지 (`.gitignore`에 등록됨)

## 브랜치/커밋

- 작업 브랜치: `feature/accessblock` (origin: `4RG0S/veyon-web`)
- 빌드 환경 패치와 기능 코드는 커밋 분리 (업스트림 리베이스 대비).
- 각자 소유 파일 위주로 작업해 충돌 최소화. `AccessBlockFeaturePlugin.cpp`·`CMakeLists.txt`는 P3가 통합.
