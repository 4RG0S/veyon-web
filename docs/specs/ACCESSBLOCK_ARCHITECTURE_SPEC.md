# CHECK NODE AccessBlock 전체 아키텍처 명세

상태: **Proposed / normative target**
관련 문서: [GAN 검토](GAN_ARCHITECTURE_REVIEW.md),
[프로세스 제어](PROCESS_CONTROL_SPEC.md), [웹 제어](WEB_CONTROL_SPEC.md)

문서의 **MUST**, **MUST NOT**, **SHOULD**, **MAY**는 구현 수용 기준을 뜻한다.

## 1. 목표와 비목표

### 목표

- master가 여러 Windows slave에 정책을 배포하고 장치별 실제 적용 결과를 확인한다.
- 머신 전역 HKLM, WFP, 프로세스 제어의 writer는 머신당 하나만 존재한다.
- 중복·지연·재전송·다중 master에서도 desired state가 결정적으로 수렴한다.
- 새 정책 실패 시 last-known-good를 유지하고, 재부팅·crash 뒤 reconcile한다.
- 브라우저 정책, WFP, 프로세스 집행의 서로 다른 보장과 수명을 숨기지 않는다.
- CHECK NODE가 소유하지 않은 OS/GPO/MDM artifact를 변경하거나 삭제하지 않는다.

### 비목표

- TLS를 복호화해 모든 앱의 HTTPS path/query를 검사하는 기능
- 현재 단계에서 커스텀 kernel/callout driver 개발
- Veyon의 모니터링, VNC, 데모 기능 복구
- antivirus, EDR, secure web gateway의 대체
- 모든 backend 효과의 완전한 ACID 보장. 이미 종료한 프로세스와 끊긴 연결은 rollback할
  수 없다.

## 2. 현재 구조에서 폐기할 전제

1. `master → server → worker`가 현재 AccessBlock의 실제 흐름이라는 전제를 폐기한다.
   현재는 server handler가 blocker를 직접 호출하고 worker handler는 처리하지 않는다.
2. `void apply()/clear()`가 충분하다는 전제를 폐기한다. 결과, warning, artifact,
   rollback/probe 정보가 필요하다.
3. 빈 URL/app 목록 두 개를 Stop으로 해석하는 관례를 v2에서 폐기한다.
4. process polling을 실행 예방, DNS 결과 IP WFP를 exact domain/URL block으로 부르지 않는다.
5. `Feature::Flag::Mode`를 정책 수명 모델로 사용하지 않는다. AccessBlock은 모니터링/데모
   mode와 배타적인 화면 mode가 아니라 독립 정책이다. 현재 mode 실행은 다른 mode를
   중지하므로 장기 접근 정책과 맞지 않는다.

## 3. 규범적 목표 구조

```text
Master process (control plane)
  PolicyEditor -> PolicyCompiler -> DeploymentCoordinator -> DeviceStatusModel
                                   │ FeatureMessage protocol v2
                                   ▼
per-WTS-session veyon-server
  AccessBlockGateway
  - remote authentication principal binding
  - AccessBlock authorization, quota, schema check
  - command/reply correlation
                                   │ authenticated local IPC
                                   ▼
veyon-service (machine singleton authority)
  AccessBlockBroker
  - assignment arbitration and effective-policy compiler
  - desired/applied journal and ownership manifests
  - serialized state machine, reconcile, audit, worker supervision
                    ┌──────────────┴──────────────┐
                    ▼                             ▼
             NetworkEnforcer               ProcessEnforcer
             - browser adapters             - OS app-control adapter
             - WFP adapters                 - containment fallback
             - DNS resolver                 - identity resolver
```

### 3.1 Master

Master MUST:

- typed rule을 입력받아 slave capability와 비교한 뒤 배포한다.
- assignment별 revision과 exact payload hash를 영속 저장한다.
- 장치별 `desired`, `accepted`, `applied`, `health`를 구분해 표시한다.
- 전송 API 반환값을 적용 성공으로 표시하지 않는다.
- timeout 때 먼저 `QueryStatus`를 보내 observed revision/hash를 확인한다. 불일치할 때만
  같은 command ID를 backoff+jitter로 재전송한다.

### 3.2 세션별 server gateway

Windows Veyon Service는 WTS 세션별로 server를 생성한다. 따라서 gateway MUST NOT:

- 머신 전역 정책의 authoritative copy를 보관한다.
- HKLM, WFP, 전체 프로세스 감시를 직접 적용한다.
- 동기 DNS나 장시간 OS 작업으로 network/event loop를 막는다.

Gateway MUST:

- 인증 결과에서 얻은 controller principal을 immutable security context로 broker에 전달한다.
- payload가 주장하는 `controllerId`를 신뢰하지 않는다.
- `ManageAccessPolicy`, `ForceClearAll`, `ViewAccessAudit` 권한을 분리해 검사한다.
- 로컬 IPC 결과를 원래 `MessageContext`와 연결해 master에 reply한다.

현재 `MessageContext`는 I/O device와 connection pointer만 가지므로 인증 principal을 담는
작은 core 확장이 필요하다. 이 보안 문맥 없이는 다중 controller 소유권을 운영 수준으로
구현했다고 보지 않는다.

### 3.3 머신 단일 broker

Broker는 머신 전역 정책의 유일한 authority와 writer다. MUST:

- 모든 session gateway 요청을 단일 직렬화 queue로 처리한다.
- controller assignment, revision, lease, command deduplication을 관리한다.
- journal을 먼저 기록하고 enforcer를 조정한다.
- worker crash/deadline을 감시하고 circuit breaker와 backoff를 적용한다.
- boot와 worker restart 때 desired와 observed OS state를 reconcile한다.

동일 머신에 두 broker가 writer가 되지 않도록 서비스 단일 인스턴스와 broker 전용 mutex를
모두 사용한다. mutex만으로 server-owned 설계를 정당화하지 않는다.

### 3.4 Enforcer

목표 구조에서 Network/Process enforcer는 broker가 감독하는 별도 child process다. 하나의
backend crash가 Veyon Service나 다른 제어 영역을 종료시키지 않아야 한다.

전환용 MVP는 broker 내부의 전용 thread에서 기존 엔진을 실행할 수 있다. 단, 머신 단일
writer, deadline, typed result, journal, reconcile를 모두 만족해야 하며 다중 session
server에서 직접 실행해서는 안 된다.

선택적 session worker는 차단 안내와 사용자 세션 telemetry만 담당한다. authoritative
enforcement나 desired state를 소유하지 않는다.

## 4. 정책 모델

정책 단위는 불변 `PolicySnapshot`이다.

```text
PolicySnapshot
  protocolVersion       uint16
  schemaVersion         uint16
  assignmentId          UUID
  revision              uint64, assignment 안에서 단조 증가
  commandId             UUID, 재전송 멱등 키
  expectedPreviousHash  SHA-256 또는 null
  payloadHash           SHA-256 of exact transmitted payload bytes
  lifetime              Lease | Durable
  expiresAtUtc          Lease일 때 필수
  maxTtlSeconds         Lease일 때 필수
  active                bool
  processRules[]
  webRules[]
```

`controllerPrincipal`은 payload 필드가 아니라 인증 context에서 부여한다. Broker의 assignment
키는 `(controllerPrincipal, assignmentId)`다.

### 4.1 다중 controller 합성

- 살아 있는 모든 assignment의 deny rule 합집합을 effective policy로 계산한다.
- 일반 `ClearAssignment`는 송신 principal이 소유한 assignment에 더 높은 revision의
  tombstone을 기록한다.
- 다른 principal의 assignment는 일반 clear로 제거할 수 없다.
- `ForceClearAll`은 별도 권한, 사용자 확인, before/after hash, 감사 로그를 요구한다.
- 동일 revision/동일 hash는 멱등 성공이다.
- 동일 revision/다른 hash는 `RevisionConflict`, 낮은 revision은 `StaleRevision`이다.
- `expectedPreviousHash` 불일치는 `CompareAndSwapConflict`로 거부하고 현재 상태를 반환한다.

### 4.2 수명

- `Lease`가 기본이다. 최대 TTL은 배포 설정의 상한을 넘을 수 없고, 만료 시 broker가
  tombstone을 생성해 effective policy를 다시 계산한다.
- master 연결 종료 자체는 암묵적인 clear가 아니다.
- `Durable`은 재부팅 뒤에도 유지되며 관리자 권한과 명시적 UI 경고가 필요하다.
- Lease 갱신은 새 revision 또는 전용 renew command로 journal되어야 한다.

### 4.3 Rule type

공통 rule은 `ruleId`, `required`, `guarantee`, `scope`, `auditLabel`을 가진다. 세부 type은:

- `BrowserUrlPattern`
- `ManagedBrowserDomain`
- `NetworkCidr`
- `ResolvedDomainNetworkBestEffort`
- `ApplicationIdentity`
- `WeakExecutableNameContainment`

문자열 모양으로 type을 추론하지 않는다. 지원하지 않는 required rule은 전체 assignment를
`Rejected(UnsupportedGuarantee)`로 거부한다. best-effort rule 실패는 `Degraded`가 될 수
있지만 조용한 downgrade는 금지한다.

## 5. FeatureMessage protocol v2

동결된 wire 식별자:

```text
LegacyAccessBlockFeatureUid = dbeee12f-b78b-42cd-be9a-aec1fbd7fb2f
AccessBlockPolicyV2FeatureUid = c861b60d-d65d-4905-b52a-a33ff6904ece
protocolVersion = 2
schemaVersion = 1
FeatureMessage argument 0 = EnvelopeCbor (QByteArray, deterministic CBOR)
```

`EnvelopeCbor` 안의 `payload`는 CBOR byte string이며 `payloadHash`는 그 exact byte string의
SHA-256이다. Qt `QVariantMap` 재직렬화 결과를 hash하지 않는다. 향후 command나 필드를
추가해도 위 두 UUID와 기존 wire 정수의 의미를 바꾸지 않는다.

### 5.1 Command

```text
Request command (qint32)
  1 QueryCapabilities
  2 SetDesiredPolicy
  3 ClearAssignment
  4 RenewLease
  5 QueryStatus
  6 Reconcile
  7 ForceClearAll

Response command (qint32)
  100 Accepted
  101 ApplyResult
  102 StatusResult
  103 Rejected
  104 CapabilitiesResult
```

v2는 기존 AccessBlock v1과 **서로 다른 고정 Feature UID**를 MUST 사용한다. 논리 이름은
`AccessBlockPolicyV2FeatureUid`이며 위 UUID를 변경하지 않는다.
현재 v1 handler는 command를 검사하지 않고 누락된 URL/app 인자를 빈 목록으로 읽으므로,
같은 UID에 `QueryCapabilities`나 v2 필드만 보내면 구버전 slave가 전역 clear할 수 있다.

- v1 Feature UID에는 `BlockedUrls=0`, `BlockedApps=1`을 가진 legacy Default message만 보낸다.
- v2 command, capability probe, response는 모두 별도 v2 Feature UID를 사용한다.
- 구버전 slave가 알 수 없는 v2 UID를 무시하거나 timeout하는 것이 안전한 capability 결과다.
- v2의 권위 데이터는 exact payload byte array이며, hash는 재직렬화한 객체가 아니라 수신
  byte array에 대해 검증한다.

필수 envelope 필드:

```text
protocolVersion, schemaVersion, commandId, assignmentId, revision,
expectedPreviousHash, payloadHash, payload, requestedAtUtc
```

### 5.2 Response 의미

- `Accepted`: 인증·인가·schema/revision 검사와 desired journal 원자 기록이 끝났다.
- `Applied`: 모든 required backend가 명세된 최소 증거까지 적용하고 probe/verify했다.
  Browser backend의 기본 최소 증거는 policy artifact의 write/read-back과 conflict 부재이며,
  기존 tab의 실제 navigation 차단까지 뜻하지 않는다.
- `Degraded`: required state는 유지했지만 best-effort backend/rule 일부가 실패했다.
- `Failed`: required backend 적용이 실패해 applied는 last-known-good에 머물렀다.
- `Rejected`: desired state를 받지 않았다. unauthorized, invalid, stale, conflict,
  unsupported 등이 원인이다.
- `RecoveryRequired`: journal 또는 ownership manifest가 손상되어 자동 변경을 중단했다.

응답은 다음을 포함한다.

```text
commandId, assignmentId, desiredRevision, desiredHash,
effectiveHash, managedAppliedHash, externalObservedHash,
state, bootId, brokerInstanceId,
backendResults[], rejectedRules[], warnings[], lastError, timestamps
```

연결이 final `ApplyResult` 전에 끊기면 master는 재전송에 앞서 `QueryStatus`로 결과를 찾는다.

### 5.3 Legacy v1

- legacy Default message는 두 인자의 존재와 type을 모두 검증한다. 누락/타입 오류를 전역
  clear로 바꾸지 않는다.
- legacy message와 v2 negotiation은 Feature UID를 공유하지 않는다.
- 인증 principal별 예약 assignment로 번역하고 짧은 Lease를 부여한다.
- master UI는 ACK가 없는 구버전 slave를 `Legacy/Unverified`로 표시한다.
- fleet가 v2 broker capability를 충족하기 전까지 legacy writer와 broker가 동시에 같은
  OS artifact를 쓰는 dual-write는 금지한다.

## 6. Broker 상태 머신

```text
Inactive
  -> Preparing
       -> Active
       -> Degraded
       -> Failed (last-known-good remains applied)

Active/Degraded
  -> Preparing (new effective hash)
  -> RollingBack -> Active|Degraded|RecoveryRequired
  -> Inactive (effective policy empty and owned artifacts verified clear)

Any
  -> RecoveryRequired (journal/manifest corruption or rollback failure)
```

`desiredHash != managedAppliedHash`는 정상적으로 관찰 가능한 상태이며 master에 숨기지
않는다. `externalObservedHash`는 CHECK NODE가 소유하지 않은 browser/OS policy의 별도
관측값이며 managed applied hash에 섞지 않는다.

## 7. Apply 실행 흐름

1. Master compiler가 typed input을 정규화하고 target capability를 확인한다.
2. Master가 revision, command ID, exact payload hash를 생성하고 장치별 요청을 보낸다.
3. Gateway가 remote auth principal, AccessBlock 권한, quota, rate limit을 검사한다.
4. Broker가 deduplication, stale/CAS, lease, schema를 검사한다.
5. Broker가 `admissionValidate`로 typed syntax, protected target, static capability,
   required guarantee, 현재 외부 policy authority를 부작용 없이 검사한다. 실패하면 desired를
   기록하지 않고 `Rejected`한다.
6. Broker가 새 desired assignment를 write-ahead journal에 원자 기록한다.
7. Gateway는 `Accepted`를 반환한다.
8. Broker가 모든 backend에 `prepare`를 요청한다. 긴 DNS/서명 검증은 bounded worker
   queue에서 실행한다. runtime/capability drift로 required prepare가 실패하면 OS state를
   바꾸지 않고 `Failed`를 기록한다.
9. Broker는 모든 `PreparedChange`를 수집한다. **OS mutation 전에** exact artifact name,
   expected type/value/hash, preimage, prior/target applied hash, commit 순서, rollback token을
   operation-intent WAL에 기록하고 fsync한다.
10. reversible network/browser backend를 먼저 commit/verify한다.
11. process containment를 마지막에 활성화한다. 이 시점 이후 종료된 프로세스는 rollback할
    수 없음을 result에 남긴다.
12. 이후 required commit/verify가 실패하면 새 commit을 멈추고, 이미 commit한 reversible
    backend를 WAL token으로 역순 rollback하며 이전 process generation을 복원한다.
13. 모든 backend를 probe해 이전 managed applied hash와 일치할 때만 `Failed`로 끝낸다.
    불일치, rollback 실패 또는 비가역 효과로 state를 확정할 수 없으면 observed hash와 함께
    `RecoveryRequired`로 전환한다.
14. 성공하면 Broker가 managed applied hash, backend result, ownership manifest를 원자
    기록하고 operation intent를 committed로 표시한다.
15. Gateway/master에 `Applied` 또는 `Degraded`를 보낸다.

Backend 공통 계약:

```text
admissionValidate(policy, capabilities, observed) -> ValidationReport
prepare(plan, revision, hash) -> PreparedChange | Error
serializeIntent(prepared, previousState) -> DurableOperationIntent
commit(prepared) -> EngineResult + OwnershipDelta
verify(expectedHash) -> ObservedState
rollback(token) -> EngineResult
probe() -> ObservedState
clearOwned(manifest) -> EngineResult
```

## 8. Clear 실행 흐름

1. Master는 빈 목록 대신 높은 revision의 `ClearAssignment`를 보낸다.
2. Broker가 그 principal의 assignment tombstone을 journal한다.
3. 살아 있는 다른 assignment와 합집합해 새 effective policy를 만든다.
4. Enforcer가 exact clear plan과 preimage/rollback token을 준비하고 broker가 mutation 전에
   operation-intent WAL을 fsync한다.
5. Enforcer는 ownership manifest로 CHECK NODE 소유가 입증된 artifact만 변경한다.
6. 제거 실패 시 managed 차단이 남았음을 `Failed/RecoveryRequired`로 보고한다. inactive로
   가장하지 않는다.
7. managed observed state와 empty effective hash가 일치할 때만 `Applied(InactiveOwned)`를
   반환한다. foreign policy가 계속 차단하면 별도 `ExternallyBlocked` 관측값을 함께 보낸다.

## 9. 재부팅·crash·drift 복구

Broker 시작 순서:

1. state directory ACL과 journal checksum/schema를 검사한다.
2. desired assignments, last-known-good applied hash, ownership manifests를 읽는다.
3. Lease 만료를 계산하고 필요한 tombstone을 만든다.
4. 실제 registry/WFP/process-control 상태를 probe한다.
5. desired와 observed 차이를 reconcile한다.
6. 모든 required backend 확인 전에는 master에 `Ready/Applied`를 보고하지 않는다.

State는 Veyon의 `%GLOBALAPPDATA%` 확장 아래 `Veyon/AccessBlock/`에 두고 SYSTEM 및
Administrators만 write 가능하게 한다. 최소 파일은 다음과 같다.

```text
assignments.journal
operation-intents.journal
applied-state.json
browser-ownership.json
enforcer-health.json
audit.log
```

원자 교체에는 `QSaveFile` 또는 동등한 write-temp/fsync/rename 절차를 사용한다. desired
journal과 persistent artifact intent는 별도 phase다. registry 또는 다른 persistent OS
artifact를 변경하기 전에 prepared plan, exact artifact identity, preimage, rollback token이
fsync되어야 한다. startup은 미완료 intent를 probe해 complete 또는 rollback한 뒤에만 새
명령을 받는다.

Journal/manifest가 손상되면 foreign artifact를 추측해 대량 삭제하지 않는다.
`RecoveryRequired`로 들어가고 로컬 관리자용 `status`, `reconcile`,
`break-glass-clear`를 제공한다. break-glass는 소유가 확인된 artifact만 우선 제거하며 강제
전체 삭제는 별도 확인과 감사 로그가 필요하다.

## 10. 실패 정책

| 사건 | 요구 동작 |
| --- | --- |
| malformed/oversized/unauthorized | Rejected, 현재 상태 유지 |
| duplicate same revision/hash | 저장된 결과 재전송 |
| stale 또는 revision conflict | Rejected, 현재 revision/hash 반환 |
| admission에서 unsupported required 발견 | desired 미기록, Rejected |
| prepare 중 required backend 실패 | OS 불변, desired와 applied 불일치, Failed |
| commit/verify 중 required backend 실패 | 역순 rollback 후 LKG probe; 일치할 때만 Failed |
| rollback 불완전/관측 불명 | RecoveryRequired + backend별 observed hash |
| best-effort backend 일부 실패 | required 상태 유지, Degraded |
| clear 실패 | 남은 차단을 보고, inactive 금지 |
| enforcer crash | broker가 restart/backoff, desired 유지, Degraded |
| server session 종료 | 머신 policy 변화 없음 |
| Lease 만료 | assignment tombstone 후 reconcile |
| journal corruption | RecoveryRequired, 임의 foreign 삭제 금지 |

## 11. 보안과 자원 제한

- Remote 연결 권한과 AccessBlock 정책 변경 권한을 분리한다.
- Local broker IPC는 SYSTEM/서비스 SID DACL의 named pipe와 서비스가 발급한 단기 token을
  함께 사용한다.
- Enforcer는 Job Object, operation deadline, bounded queue, crash-rate circuit breaker를
  사용한다.
- DNS worker와 process identity worker는 불필요한 outbound/privilege를 제거한다.
- 기본 로그에는 전체 URL path/query, 사용자 정보, command payload를 남기지 않는다.
  rule ID, hash, count, error code를 사용하고 민감 감사 로그는 별도 보존 정책을 둔다.
- 반복 DNS/process 오류 로그를 rate-limit한다.

초기 hard limit:

- envelope 256 KiB
- active assignment 64개/머신
- assignment당 rule 512개
- rule string UTF-8 8 KiB
- resolved-domain rule 128개/assignment
- DNS 동시 작업 8개
- WFP address filter 4096개/머신
- Apply 기본 deadline 30초

Browser vendor의 1,000-entry 상한에는 foreign entry도 포함한다. 실제 observed count가
상한을 넘으면 변경 전 `Rejected(CapacityExceeded)`해야 한다.

## 12. 구현 모듈 경계

권장 파일 경계:

```text
plugins/accessblock/
  protocol/AccessBlockMessageCodec.*
  policy/PolicyTypes.*
  policy/PolicyNormalizer.*
  policy/PolicyCompiler.*
  gateway/AccessBlockGateway.*
  broker/AccessBlockBroker.*
  broker/PolicyJournal.*
  broker/AssignmentStore.*
  ipc/AccessBlockIpcClient.*
  ipc/AccessBlockIpcServer.*
  enforcers/network/...
  enforcers/process/...
  ui/AccessBlockPolicyPage.*
  ui/AccessBlockDeploymentModel.*
```

필요한 core 변경은 최소한 다음으로 제한한다.

- 인증 principal/capability를 `MessageContext`에 노출
- service component의 broker lifecycle hook 또는 AccessBlock service interface
- 필요 시 final async reply correlation 지원

core 변경을 피하려고 message가 주장하는 controller ID를 신뢰하거나 per-session server를
authority로 유지해서는 안 된다.

## 13. 마이그레이션

1. **현상 고정:** legacy 동작, 외부 browser policy, 다중 WTS session, crash/reboot를
   재현하는 테스트를 작성한다. README의 구현 상태를 실제와 맞춘다.
2. **즉시 P0 보강:** 전체 registry key 삭제 금지, WFP transaction/last-known-good,
   process handle identity 재검증을 먼저 적용한다. 이 단계도 운영 승인 완료는 아니다.
3. **Protocol v2:** typed envelope, revision/hash, status/ACK를 추가하고 legacy는 read-only
   compatibility로 둔다.
4. **Broker shadow:** service broker가 journal/compile/probe만 하고 legacy가 유일 writer로
   남는다. 결과가 일치할 때까지 dual-write하지 않는다.
5. **Broker cutover:** machine singleton을 authority로 전환한다. multi-session test를 Gate로
   둔다.
6. **Process cutover:** broker enforcer 활성화 후 legacy timer를 중단한다.
7. **WFP cutover:** 새 generation을 transaction commit한 뒤 legacy writer를 중단한다.
8. **Browser migration:** 기존 key 전체를 CHECK NODE 소유로 간주하지 않는다. 입증된 값만
   ownership manifest로 가져오고 foreign 값은 보존한다.
9. **Worker 격리:** Network/Process child process와 supervision을 적용한다.
10. **Legacy 제거:** 대상 fleet가 v2/broker capability 기준을 충족한 뒤 v1 writer를 제거한다.

## 14. 전체 수용 Gate

- Windows/MSYS2 compile과 static analysis가 통과한다.
- 서비스 설치 상태에서 WTS 2개 이상을 열어도 broker/enforcer가 머신당 하나다.
- 30대 상당의 가상 target에서 offline/timeout을 성공으로 세지 않는다.
- duplicate, out-of-order, stale Stop, 다중 controller, Lease expiry가 결정적이다.
- service/server/enforcer kill과 재부팅 뒤 desired/applied가 수렴한다.
- backend fault injection 때 last-known-good와 ownership이 보존된다.
- raw URL/path와 민감 payload가 기본 로그에 남지 않는다.
- 프로세스/웹 상세 문서의 Gate가 모두 통과한다.
- 최소 master 1대, slave 2대의 실제 Windows 종단간 시험 결과가 기록된다.

이 Gate 전에는 `Applied`라는 UI/로그 상태를 운영 성공의 증거로 사용하지 않는다.
