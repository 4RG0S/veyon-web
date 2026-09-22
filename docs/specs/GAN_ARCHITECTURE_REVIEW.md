# AccessBlock GAN 아키텍처 검토 기록

상태: **Final decision record**
검토 대상: `README.md`, `docs/team/P1~P3`, `plugins/accessblock/`, Windows 서비스와
FeatureMessage 흐름

## 1. 방법

토론은 다음 역할로 분리했다.

- **Generator**: 기존 Veyon 플러그인 구조를 최대한 활용하면서 더 나은 구조와 실행
  흐름을 제안한다.
- **Discriminator**: 오탐, 우회, 외부 정책 침범, 부분 실패, 재시작, 다중 세션·다중
  master 반례로 제안을 공격한다.
- **Control specialist/Judge**: Windows의 실제 집행 수단과 제품 비용을 비교하고 최종
  보장 수준과 수용 기준을 판정한다.

판정은 문서 주장보다 실제 소스의 실행 경로를 우선했다. 이전 로컬 보강 변경은 보존했고
Windows/MSYS2 빌드나 2대 실기기 시험은 수행하지 않았다.

## 2. 입력 명세와 실제 코드의 불일치

| 항목 | 문서 주장 | 실제 확인 | 판정 |
| --- | --- | --- | --- |
| 실행 흐름 | master → server → worker | server가 두 blocker를 직접 호출하고 worker handler는 `false` | 문서 오류 |
| 구현 상태 | 차단 로직 미구현 | Process polling, Chrome/Edge policy, WFP가 존재 | 문서 오류 |
| URL 제어 | 브라우저 URL/도메인 차단 | bare domain은 DNS 결과 IP만 WFP에서 차단 | 보장 과장 |
| 프로그램 제어 | 실행 차단 | 1초 폴링 뒤 basename 일치 프로세스 강제 종료 | 보장 과장 |
| 적용 성공 | Start/Stop 성공 | 전송·handler 반환값일 뿐 실제 적용 ACK 없음 | 보장 부재 |

근거:

- [`AccessBlockFeaturePlugin.cpp`](../../plugins/accessblock/AccessBlockFeaturePlugin.cpp#L46-L131)
- [`UrlBlocker.cpp`](../../plugins/accessblock/UrlBlocker.cpp#L141-L203)
- [`ProcessBlocker.cpp`](../../plugins/accessblock/ProcessBlocker.cpp#L138-L218)
- [`README.md`](../../README.md#L78-L93)

## 3. Round 1 — Generator 제안

첫 제안은 플러그인 내부에 정책 controller를 두고 server가 Network/Process worker로
명령을 전달하는 구조였다. 목록 대신 versioned policy를 사용하고, ACK, retry,
last-known-good, backend 결과를 추가한다는 점은 기존 구조보다 개선이다.

```text
Master -> veyon-server -> AccessPolicyController -> Network/Process worker
```

Generator는 현재 폴링과 WFP를 폐기하기보다 다음처럼 보장 이름을 낮추고 adapter로
격리하자고 제안했다.

- process polling: `ProcessContainmentBackend`
- DNS 결과 IP WFP: `ResolvedDomainNetworkBackend`
- Chrome/Edge registry: `ManagedBrowserPolicyBackend`
- 강한 실행 예방: optional OS application-control backend

## 4. Round 1 — Discriminator 공격

다음은 운영 배포를 막는 P0 반례다.

### P0-1. 외부 브라우저 정책 파괴

`writePolicyKey()`와 `clear()`는 `RegDeleteTreeW()`로 Chrome/Edge의 `URLBlocklist`
전체 키를 삭제한다. CHECK NODE가 만들지 않은 GPO/MDM/관리자 정책도 사라질 수 있고,
Chrome 성공 후 Edge 실패 시 양쪽 키를 다시 삭제한다.

근거: [`UrlBlocker.cpp`](../../plugins/accessblock/UrlBlocker.cpp#L84-L136),
[`UrlBlocker.cpp`](../../plugins/accessblock/UrlBlocker.cpp#L206-L245)

### P0-2. WFP 교체 중 fail-open과 부분 적용

DNS 일부만 성공해도 기존 WFP generation 전체를 먼저 지운다. 이후 engine open,
sublayer, 개별 filter 추가 중 하나가 실패하면 last-known-good가 사라지거나 일부 필터만
남는다. 명시적 WFP transaction도 없다.

근거: [`UrlBlocker.cpp`](../../plugins/accessblock/UrlBlocker.cpp#L250-L360),
[Microsoft WFP best practices](https://learn.microsoft.com/en-us/windows/win32/fwp/best-practices)

### P0-3. 분산 desired state 부재

정책 ID, revision, hash, controller principal, lease, ACK, 상태 질의, stale 명령 거부가
없다. 끊긴 장치도 master가 성공처럼 취급할 수 있고, 지연된 Stop이나 다른 master의 빈
목록이 최신 정책을 해제할 수 있다. `FeatureMessage` reply API는 이미 있으므로 기반의
필연적 한계가 아니다.

근거: [`AccessBlockFeaturePlugin.cpp`](../../plugins/accessblock/AccessBlockFeaturePlugin.cpp#L46-L120),
[`VeyonServerInterface.h`](../../core/src/VeyonServerInterface.h#L44-L48)

### P0-4. basename 기반 사후 강제 종료

경로를 버린 basename만 비교하므로 rename 우회와 동명이인 오종료가 동시에 가능하다.
snapshot 이후 PID 재사용, 자식 프로세스 생존, 최대 1초 실행 창, 저장 중 데이터 손상도
고려되지 않는다. Microsoft는 `TerminateProcess`가 비동기이며 DLL 상태를 손상시킬 수
있다고 명시한다.

근거: [`ProcessBlocker.cpp`](../../plugins/accessblock/ProcessBlocker.cpp#L43-L77),
[`ProcessBlocker.cpp`](../../plugins/accessblock/ProcessBlocker.cpp#L175-L218),
[Microsoft TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)

### P0-5. 머신 전역 집행자의 세션별 중복

Windows Veyon Service는 활성 WTS 세션마다 `VeyonServerProcess`를 만든다. 현재 blocker는
server 플러그인 멤버이므로 세션 수만큼 HKLM writer, WFP engine, 전체 프로세스 poller가
생긴다. 한 세션 서버의 종료가 다른 서버가 필요로 하는 정책을 지울 수도 있다. 기존
`FeatureWorkerManager`도 server 인스턴스에 속하므로 단순 worker 전달로 해결되지 않는다.

근거: [`WindowsServiceCore.cpp`](../../plugins/platform/windows/WindowsServiceCore.cpp#L111-L179),
[`AccessBlockFeaturePlugin.h`](../../plugins/accessblock/AccessBlockFeaturePlugin.h#L95-L103),
[`FeatureWorkerManager.h`](../../core/src/FeatureWorkerManager.h#L72-L89)

## 5. Round 2 — Generator 수정과 재반박

Generator는 P0-1~5를 수용하고 초기 server-owned controller 안을 폐기했다. 최종 수정안은
세션별 server를 인증 gateway로만 두고, 머신당 하나인 서비스 broker가 정책의 단일
권위자가 되는 구조다.

```text
Master
  -> per-session veyon-server: remote authentication, authorization, limits, reply correlation
  -> AccessBlockBroker in veyon-service: desired/applied journal, arbitration, reconcile
  -> NetworkEnforcer / ProcessEnforcer: machine-global data plane
```

“현재 server에 ACK만 추가하면 더 단순하다”는 반론은 거부했다. ACK는 분산 상태 문제의
일부만 해결하며, 다중 WTS 세션의 중복 writer와 서로 다른 artifact 수명 문제는 남기
때문이다.

## 6. Round 2 — Discriminator 재공격과 수정

수정안도 그대로 승인하지 않고 crash 경계와 구버전 호환을 다시 공격했다. 다음 네 결함을
발견해 최종 명세에 반영했다.

1. **v1/v2 Feature UID 분리:** 현재 v1 handler는 command를 검사하지 않고 누락 인자를 빈
   목록으로 읽는다. 같은 UID로 v2 capability query를 보내면 구버전 slave가 전역 clear할
   수 있으므로 v2는 별도 고정 UID를 사용한다.
2. **admission과 Accepted 순서:** 지원하지 않는 required rule은 desired journal과
   `Accepted` 전에 `Rejected`한다. Accepted 뒤 runtime drift/prepare 실패는 `Failed`다.
3. **mutation 전 durable intent:** browser registry 등 persistent artifact를 쓰기 전에
   exact artifact 이름, preimage, commit 순서, rollback token을 operation-intent WAL에
   fsync한다. 결과만 나중에 기록하는 방식은 crash 시 orphan을 만든다.
4. **부분 적용의 역순 보상:** 뒤 backend 실패 시 앞서 commit한 reversible backend를 역순
   rollback한다. probe가 이전 last-known-good를 확인할 때만 `Failed`이며, 불명확하거나
   rollback이 실패하면 observed hash와 함께 `RecoveryRequired`다.

이 공격을 거친 backend 계약은
`admissionValidate/prepare/serializeIntent/commit/verify/rollback/probe`로 확정했다.

## 7. Round 3 — Wire/schema 판별과 동결

두 번째 판별은 P0를 통과했지만 병렬 구현 전에 남은 P1 해석 차이를 공격했다. 그 결과:

- v2 Feature UID를 `c861b60d-d65d-4905-b52a-a33ff6904ece`로 발급하고 request 1~7,
  response 100~104의 qint32 command 값을 동결했다.
- `Observe/Contain/Prevent`와 action의 허용 조합을 닫힌 집합으로 만들었다.
- 위치 독립 publisher/hash/package identity와 위치 종속 canonical-path Gate를 분리했다.
- containment SLO에 5분 window, 최소 N=100, nearest-rank, timeout/실패 `+infinity` 규칙을
  정했다.
- browser profile class를 schema/capability/Applied 판정에 포함했다.

이 보완 뒤 명세 승인은 구현 또는 운영 배포 승인을 뜻하지 않는다. 아래 Gate의 실기기
증거가 별도로 필요하다.

마지막 schema 판별에서 발견한 약한 이름 `Prevent`, SLO 초기 표본, 미래 browser profile,
identity 조합의 모호성도 제거했다. `WeakExecutableName+Prevent`는 전면 거부하고, required
SLO의 증거 부족은 `Degraded(PendingEvidence)`, profile 범위는 현재 capability schema의
열거 집합으로 한정했다. 복합 identity는 `allOf IdentityClause[]`로 명시했다.

## 8. Judge 최종 판정

### 채택

1. **머신 단일 broker**를 정책의 유일한 writer와 authority로 둔다.
2. master는 `send 성공`이 아니라 장치별 `Applied/Degraded/Failed`를 표시한다.
3. 정책은 `(인증된 controller principal, assignmentId, revision)` 단위로 저장한다.
4. 여러 controller의 살아 있는 assignment는 deny 합집합으로 계산한다.
5. 일반 clear는 자기 assignment의 높은 revision tombstone만 만든다. 전체 해제는 별도
   `ForceClearAll` 권한이 필요하다.
6. 기본 수명은 만료되는 `Lease`; `Durable`은 관리자 전용이다.
7. browser, WFP, process backend는
   `admissionValidate/prepare/serializeIntent/commit/verify/rollback/probe` 결과를 반환한다.
   `void apply()/clear()`는 폐기한다.
8. 현재 process polling과 DNS-IP WFP는 호환용 best-effort backend로만 유지한다.
9. 실행 예방이 요구되면 App Control for Business/AppLocker 같은 지원되는 OS 정책을
   capability로 요구하고, 불가능한 장치에서 조용히 polling으로 downgrade하지 않는다.
10. exact URL/path와 all-application domain block은 서로 다른 보장으로 노출한다.

### 기각

- `veyon-server` 또는 그 `FeatureWorkerManager`를 머신 전역 정책 authority로 사용
- URL/프로그램 두 `QStringList`와 빈 목록 Stop 관례를 v2 핵심 계약으로 유지
- `RegDeleteTreeW()`로 browser policy key 전체 소유
- 기존 WFP generation을 먼저 지운 뒤 새 generation 생성
- IFEO `Debugger`를 일반 프로그램 차단 수단으로 사용
- AppLocker/WDAC 조직 정책을 수업 Start/Stop 때마다 플러그인이 직접 덮어쓰기
- DNS 결과 IP 차단을 정확한 FQDN/URL 차단으로 표기
- 1초 폴링 종료를 실행 예방으로 표기
- 커스텀 kernel driver를 첫 구현 단계에 도입

## 9. 배포 승인 전 공통 Gate

- 다중 WTS 세션에서 broker/enforcer가 머신당 정확히 하나만 실행된다.
- duplicate/out-of-order/stale Start·Stop과 다중 master 합집합이 결정적으로 동작한다.
- 외부 Chrome/Edge policy를 Apply/Update/Clear/Crash 전후 byte-for-byte 보존한다.
- WFP fault injection 시 이전 generation이 유지되고 차단 공백이 없다.
- 재부팅과 service/server/enforcer crash 후 journal 기반 상태가 일관되게 복원된다.
- 강한 application identity에서 rename, 동일 basename, PID reuse, protected-name spoofing이
  무관 프로세스 종료나 우회를 만들지 않는다. legacy `WeakExecutableName`은 이 Gate 대상이
  아니며 rename/동명이인 한계를 UI와 ACK에 표시한다.
- unsupported guarantee는 `Rejected`되고 UI가 성공으로 표시하지 않는다.
- Windows/MSYS2 빌드, 서비스 설치, 최소 두 slave 실기기 시험이 완료된다.

위 Gate를 통과하기 전 현재 기능은 연구/개발용 best-effort 구현이며 운영 배포 준비가 된
차단 제품으로 보지 않는다.
