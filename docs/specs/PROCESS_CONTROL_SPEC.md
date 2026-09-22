# CHECK NODE 프로세스 제어 상세 명세

상태: **Proposed / normative target**
상위 명세: [ACCESSBLOCK_ARCHITECTURE_SPEC.md](ACCESSBLOCK_ARCHITECTURE_SPEC.md)

## 1. 보장 수준

프로세스 제어는 다음 세 보장을 명시적으로 구분한다.

| 보장 | 의미 | 허용 backend |
| --- | --- | --- |
| `Prevent` | 대상 코드가 실행되기 전에 OS가 거부 | 사전 provision된 App Control/AppLocker 계층 |
| `Contain` | 실행 이벤트를 탐지해 identity 확인 후 종료 | event observer + snapshot reconciliation |
| `Observe` | 탐지·감사만 하고 종료하지 않음 | audit observer |

현재 1초 snapshot + `TerminateProcess` 구현은 `Contain`의 초기 fallback일 뿐 `Prevent`가
아니다. UI, ACK, 로그, 문서는 이 차이를 MUST 표시한다. `Prevent`를 요구한 정책을
`Contain`으로 조용히 downgrade해서는 안 된다.

## 2. 위협 모델과 한계

### 방어 대상

- 표준 사용자 세션에서 금지된 일반 Win32/packaged app 실행
- 파일 이름 변경, 다른 경로 복사, 동일 basename에 의한 기본 오탐
- 중복·stale 정책, observer 누락, enforcer restart
- PID 재사용과 snapshot/open 사이의 TOCTOU

### 비보장

- kernel, PPL, 보안 제품 우회나 관리자/root 수준 공격자
- driver, boot-start code, 이미 kernel에 적재된 code 차단
- containment backend에서 첫 instruction 이전의 차단
- 강제 종료된 app의 미저장 데이터 복구
- 다른 자격 증명·머신·VM 안의 프로세스 제어

`TerminateProcess`는 비동기이고 DLL 전역 상태를 손상시킬 수 있다. 구현은 성공 반환만으로
종료 완료를 주장하지 않고 `WaitForSingleObject`로 bounded 확인해야 한다.
[Microsoft TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)

## 3. 컴포넌트

```text
ProcessEnforcer
  ProcessPolicyCompiler
  ProtectedIdentityRegistry
  OsApplicationControlBackend     // optional Prevent
  ProcessObserver                 // ETW/WMI capability adapter
  SnapshotReconciler              // 누락 보완
  ProcessIdentityResolver
  ProcessActionExecutor
  ProcessEventJournal / Metrics
```

ProcessEnforcer는 machine singleton이며 broker의 child process다. per-session
`veyon-server`마다 poller를 만들지 않는다.

## 4. 정책 모델

```text
ProcessRule
  ruleId              UUID
  guarantee           Prevent | Contain | Observe
  required            bool
  identity            allOf IdentityClause[1..4]
    IdentityClause    one of:
      PublisherIdentity
        publisher
        productName
        originalFileName
        minVersion / maxVersion optional
      Sha256Identity
        digest
      CanonicalPathIdentity
        path
        expectedPublisher optional
      PackageFamilyIdentity
        packageFamilyName
      WeakExecutableName
        basename
  scope
    userSids[]         empty = managed interactive users
    sessionIds[]       empty = managed interactive sessions
  action              optional; Prevent에서는 MUST be absent
    AuditOnly         Observe에서만 허용
    ImmediateForce    Contain에서만 허용
    GracefulThenForce Contain에서만 허용
  gracefulTimeoutMs   GracefulThenForce에서만 필수
  graceFailureAction  Force | ReportOnly, GracefulThenForce에서만 필수
  actionDeadlineMs    Contain에서 필수
  containmentSloProfileId optional; explicit SLO와 상호 배타적
  containmentSloP95Ms optional, process creation부터 verified termination까지
  containmentSloP99Ms optional, MUST be >= p95
  includeDescendants   bool, default false
```

Microsoft AppLocker도 publisher, path, file hash를 주요 identity 조건으로 사용한다.
Publisher는 update에 상대적으로 강하고, hash는 정확하지만 update마다 바뀌며, 사용자 쓰기
가능 경로의 path rule은 우회 위험이 있다.
[Microsoft AppLocker rules](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/applocker/working-with-applocker-rules)

### 4.1 WeakExecutableName

- legacy 호환 전용이다.
- trim, basename 추출, Unicode case-fold, Windows 실행 파일 이름 검증을 한다.
- 원래 full path를 입력해도 basename으로 약화되는 것을 숨기지 않고 warning을 반환한다.
- required 여부와 무관하게 `guarantee=Prevent`와 함께 사용할 수 없다.
- 다른 IdentityClause와 조합할 수 없고 단독 legacy clause로만 허용한다.
- UI에 rename 우회와 동일 이름 오탐 위험을 표시한다.

## 5. 정규화와 검증

Compiler MUST:

1. 모든 rule ID의 중복을 거부한다.
2. path를 환경 변수 미확장 문자열로 저장하지 않고 target 머신에서 canonicalize한다.
3. `..`, device path, reparse point, UNC, alternate data stream의 허용 정책을 명시적으로
   검사한다.
4. `identity.allOf`의 모든 clause가 일치할 때만 match한다. clause는 1~4개이며 같은 kind의
   중복과 `WeakExecutableName`+다른 clause 조합을 거부한다.
5. user SID/session scope를 표준 사용자 세션으로 제한한다. session 0은 기본 보호다.
6. 보호 대상과 겹치는 rule을 조용히 제거하지 않고 `RejectedProtectedTarget`으로 반환한다.
7. 지원하지 않는 required identity/guarantee를 `UnsupportedCapability`로 거부한다.
8. rule count, path length, hash/signature work quota를 적용한다.
9. `Observe`는 `AuditOnly`만, `Contain`은 `ImmediateForce` 또는 `GracefulThenForce`만
   허용한다. `Prevent`에는 action 필드를 금지하고 사전 provision된 OS backend가 거부를
   집행한다. 다른 조합은 `Rejected(InvalidGuaranteeAction)`이다.
10. `gracefulTimeoutMs`와 `graceFailureAction`은 `GracefulThenForce`에서만 허용하고 timeout
    상한을 검증한다. `actionDeadlineMs`는 모든 `Contain` rule에 필수다.
11. required `Contain` rule은 p95/p99 SLO를 직접 지정하거나 target capability가 광고한
    versioned `containmentSloProfileId`를 참조해야 한다. 둘을 동시에 지정할 수 없고 p95는
    p99보다 클 수 없다.
12. `WeakExecutableName`에 `guarantee=Prevent`가 있으면 required 값과 관계없이
    `Rejected(InvalidGuaranteeIdentity)`한다.

## 6. 보호 대상 불변식

다음 대상은 일반 원격 정책으로 종료할 수 없다.

- PID 0, PID 4, broker/enforcer/service의 현재 PID
- session 0의 Windows/Veyon service process
- 현재 Veyon 설치 디렉터리의 신뢰된 signer/path/file identity
- Windows 핵심 디렉터리의 Microsoft-signed 필수 프로세스
- PPL 또는 identity를 안전하게 확인할 수 없는 보호 프로세스
- policy/audit/break-glass 구성요소

보호는 basename 목록만으로 판단하지 않는다. 최소한 process handle에서 얻은 final image
path, file identity, signer, session/user를 조합한다. 공격자가 일반 파일을
`svchost.exe` 또는 `veyon-service.exe`로 rename했다고 보호 대상으로 간주해서도 안 된다.

보호 registry는 제품 update 시 검증되는 signed manifest로 배포한다. manifest 실패 시
enforcer는 fail-safe로 containment 활성화를 거부하고 `RecoveryRequired`를 보고한다.

## 7. Identity 확인 절차

Process event의 PID와 snapshot basename은 hint일 뿐 권위 identity가 아니다.

1. PID, event creation time, event generation을 queue에 넣는다.
2. `OpenProcess`로 query/synchronize 권한을 가진 handle을 연다. action이 필요할 때만
   `PROCESS_TERMINATE`를 요청한다.
3. handle에서 PID와 creation time을 다시 읽어 event와 대조한다.
4. `QueryFullProcessImageNameW`/동등 API로 full image path를 얻는다.
5. file handle을 열어 final path, volume/file ID, size, last-write time을 얻는다.
6. token에서 user SID, integrity level, session ID를 얻는다.
7. rule이 요구할 때 Authenticode signer, original file name, product, version, SHA-256을
   검증한다.
8. `ProtectedIdentityRegistry`를 다시 검사한다.
9. 현재 active policy generation과 rule이 여전히 같은지 확인한다.
10. action 직전 process가 이미 종료됐는지 확인한다.

Identity cache key는 단순 path가 아니라 최소
`(volume serial, file ID, size, last-write time)`이다. signer/hash 검증 실패를 match로
간주하지 않는다. cache는 policy generation과 product update에 따라 무효화한다.

## 8. Observer와 reconciliation

### 8.1 Event path

가능한 target에서는 ETW 또는 WMI 기반 process-start observer를 사용한다. adapter는
capability probe, reconnect, event loss counter, operation deadline을 제공한다.

Event callback은 OS 적용이나 서명 검증을 직접 하지 않는다. bounded MPSC queue에 최소
event만 기록하고 resolver worker가 처리한다. queue overflow는 조용히 버리지 않고
`Degraded(EventLoss)`와 counter를 남긴다.

### 8.2 Snapshot path

`CreateToolhelp32Snapshot` reconciliation은 다음 용도로 유지한다.

- observer 시작 전 이미 실행 중인 대상 처리
- event drop/reconnect 누락 보완
- applied policy와 실제 process 상태의 주기 검증

기본 reconciliation 주기는 1초이며 부하 시험 후 조정한다. observer가 없으면 이 backend의
보장은 `Contain/weak-latency`로 보고하고 1초 동안 코드가 실행될 수 있음을 UI에 표시한다.

### 8.3 Burst와 backpressure

- queue와 identity worker 수는 bounded다.
- 같은 `(file identity, rule ID)`의 반복 event는 짧은 창에서 합친다.
- 시스템 부하가 임계값을 넘으면 보호 대상 검증을 생략하지 않고 `Degraded`로 전환한다.
- 반복 권한 오류와 종료 오류 로그를 rate-limit한다.

## 9. Action 실행

### 9.1 AuditOnly

identity, rule ID, session, 결과만 기록한다. 원문 사용자 경로나 command line은 기본 로그에
남기지 않는다.

### 9.2 ImmediateForce

1. identity와 보호 대상을 최종 재검증한다.
2. `TerminateProcess`를 호출한다.
3. `WaitForSingleObject`를 bounded timeout으로 기다린다.
4. 종료 code/timeout/권한 오류를 result로 기록한다.
5. timeout이면 무한 재시도하지 않고 rate-limited reconciliation 대상으로 남긴다.

### 9.3 GracefulThenForce

interactive session helper가 명시적 UI close를 보낼 수 있을 때만 시도한다. grace timeout
후 같은 identity를 다시 확인한 다음 강제 종료한다. 사용자 확인 창으로 정책을 무한 지연할
수 없다. helper가 없거나 graceful close가 실패하면 `graceFailureAction=Force`일 때만 강제
종료하고, `ReportOnly`면 종료하지 않은 `Failed(GraceUnavailable)`를 반환한다.

### 9.4 Descendant

부모 PID만 보고 모든 descendant를 종료하지 않는다. PID 재사용과 정상 child 오종료를
막기 위해 creation time과 parent relation을 확인하고, `includeDescendants`가 true여도 각
child에 보호/identity 정책을 적용한다. `Prevent`가 필요한 escape 방어를 descendant
사후 종료로 대체하지 않는다.

## 10. OS application-control backend

`Prevent`는 사전 provision된 지원 OS 계층에서만 제공한다.

### 10.1 AppLocker

- audit-only canary와 event log 수집 후 enforce한다.
- 조직의 기존 GPO/MDM policy authority와 충돌하지 않아야 한다.
- deny rule은 항상 우선하고, rule collection에 rule이 생기면 allow semantics가 바뀔 수
  있으므로 CHECK NODE가 임의로 단일 deny만 삽입해서는 안 된다.
- default allow와 packaged app collection을 포함한 전체 policy 설계가 검증돼야 한다.
- publisher/path/hash별 update와 우회 시험을 통과해야 한다.

### 10.2 App Control for Business

강한 enterprise 실행 제어가 필요한 환경의 권장 기반이다. 다만 allow-oriented 정책,
호환성·배포·지원 절차가 필요하므로 수업 Start/Stop마다 plugin이 전역 policy를 덮어쓰는
수단으로 사용하지 않는다. 별도 관리자 provision과 policy adapter 계약을 사용한다.
[Microsoft Application Control for Windows](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/appcontrol)

### 10.3 금지 방식

- IFEO `Debugger` raw registry를 일반 차단 backend로 사용하지 않는다. debugger/진단과
  충돌하고 재부팅 뒤 orphaned block을 만들 수 있다.
- 이름 기반 차단만을 위해 custom kernel driver를 우선 도입하지 않는다. signed driver,
  HVCI/HLK, BSOD와 kernel attack surface 비용은 OS application control보다 크다.

## 11. Policy apply/clear/reconcile

### Apply

1. 모든 rule과 capability를 validate한다.
2. OS `Prevent` adapter 변경을 prepare/audit한다.
3. immutable containment rule index와 protected manifest를 build한다.
4. observer/reconciler를 준비하되 아직 새 generation을 노출하지 않는다.
5. required OS backend를 commit/verify한다.
6. broker가 process policy generation pointer를 한 번에 swap한다.
7. 즉시 snapshot reconciliation으로 이미 실행 중인 대상을 검사한다.
8. rule별 `Applied`, `Degraded`, `Rejected`를 broker에 반환한다.

### Clear

1. 다른 controller assignment와 합성한 새 effective rule set을 만든다.
2. immutable generation을 swap한다.
3. 남은 containment rule이 없으면 observer/reconciler를 중지한다.
4. CHECK NODE 소유가 확인된 OS policy artifact만 제거한다.
5. 이미 종료된 process는 복원하지 못한다는 `IrreversibleEffects`를 결과에 남긴다.

### Reconcile

- observer health, queue loss, active generation, OS application-control observed hash를 비교한다.
- drift가 CHECK NODE 소유 artifact라면 desired로 복원한다.
- external authority와 충돌하면 덮어쓰지 않고 `ExternalPolicyConflict`를 보고한다.

## 12. Result와 telemetry

Backend result 최소 필드:

```text
backend, policyHash, generation, state, guarantee,
ruleResults[], activeRuleCount, observerHealth, eventLossCount,
matchedCount, terminatedCount, terminationFailedCount,
protectedRejectCount, identityFailureCount, latency, lastError
```

Containment latency는 OS가 제공하는 process creation timestamp부터 종료 handle의 signaled
상태를 확인한 시점까지 측정한다. UTC 기준 5분 고정 window마다 해당 rule에 실제 match한
process를 표본으로 삼고, 최소 표본 수는 100개다. verified termination이 없거나
`actionDeadlineMs`에 도달한 표본은 latency `+infinity`로 percentile 순위에 포함한다.
nearest-rank 방식(`ceil(p*N)`)으로 계산하며 clock discontinuity로 무효인 표본 수는 별도
보고한다. 유효 표본이 100개 미만이면 `SloState=InsufficientSample`이고 SLO 충족을 주장하지
않는다. required SLO가 있는 assignment의 enforcement는 계속 활성 상태로 두되 aggregate
정책 상태를 `Degraded(PendingEvidence)`로 결정한다. N>=100인 window가 기준을 통과하면
`Applied`, 요청된 p95/p99를 넘으면 `Degraded(SloMiss)`로 전이한다. 매 window 시작 때 이전
판정을 즉시 지우지 않고, 마지막 N>=100 판정은 최대 24시간 유효하다. 24시간 동안 새
qualifying window가 없으면 다시 `Degraded(PendingEvidence)`다. 결과는 window, 전체/무효
표본 수, 두 percentile, 마지막 qualifying window 시각을 반환한다.

Process event는 rule ID와 file identity hash 중심으로 기록한다. full path, user SID,
command line은 민감 audit가 명시적으로 켜진 경우에만 제한 보존한다.

## 13. 필수 시험

### Unit

- case-fold, Unicode, extension, canonical path, reparse point, UNC, ADS
- publisher/product/original name/version과 SHA-256 matching
- identity cache invalidation
- protected manifest와 `RejectedProtectedTarget`
- revision swap 중 old event 처리

### Windows integration

- publisher/hash/package identity의 금지 app rename/copy, 동일 basename/다른
  publisher·path, protected-name spoof
- canonical-path rule의 path 내부 match와 move/rename 시 out-of-scope 판정
- `WeakExecutableName`의 rename 우회와 동명이인 match가 예상대로 재현되고 UI/ACK warning이
  유지되는지 확인
- snapshot 후 PID 종료·재사용
- session 0, 표준 사용자, 다른 interactive session, PPL/권한 부족
- child spawn, rapid exec burst, observer disconnect/drop, queue overflow
- `TerminateProcess` success/async wait/timeout과 저장 중 강제 종료 경고
- enforcer kill/restart, service reboot, Lease expiry
- AppLocker audit→enforce와 기존 GPO 공존

### Acceptance Gate

- `Prevent` policy에서 금지 code가 한 instruction도 실행하지 못한다. 이 Gate를 polling
  backend로 통과했다고 주장할 수 없다.
- `Contain` policy는 명시한 p95/p99 탐지·종료 latency를 실기기에서 측정해 UI에 보장
  class와 함께 기록한다. 요청 SLO를 넘으면 `Degraded(SloMiss)`, N<100 또는 증거가 24시간
  경과하면 `Degraded(PendingEvidence)`다.
- publisher/original-file-name, SHA-256, package-family처럼 위치 독립을 명시한 identity는
  rename/copy 뒤에도 rule 의미대로 match하고 동명이인을 오종료하지 않는다.
- `CanonicalPathIdentity`는 지정한 canonical path의 파일만 보장한다. 다른 경로로
  move/rename한 실행 파일은 규칙 범위 밖이며, 위치 독립 차단 Gate를 통과했다고 표시하지
  않는다.
- `WeakExecutableName`은 rename 저항이나 동명이인 안전 Gate를 통과했다고 표시하지 않으며,
  legacy warning과 `Contain/weak-identity` 표기를 강제한다.
- protected identity를 원격 rule로 종료할 수 없다.
- process 종료의 비가역성이 ApplyResult에 남는다.
- broker/enforcer가 머신당 하나이고 WTS 세션 추가가 poller 수를 늘리지 않는다.
