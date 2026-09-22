# CHECK NODE 웹 제어 상세 명세

상태: **Proposed / normative target**
상위 명세: [ACCESSBLOCK_ARCHITECTURE_SPEC.md](ACCESSBLOCK_ARCHITECTURE_SPEC.md)

## 1. 보장 수준

웹 제어는 다음을 하나의 `QStringList`로 섞지 않는다.

| Rule/보장 | 의미 | 주 backend |
| --- | --- | --- |
| `ManagedBrowserDomain` | 관리형 browser의 host/subdomain 요청 제어 | Chrome/Edge policy |
| `BrowserUrlPattern` | 관리형 browser의 scheme/host/port/path/query pattern | browser policy |
| `NetworkCidr` | 모든 앱의 새 outbound 연결을 IP/CIDR/port 기준 제어 | WFP |
| `ResolvedDomainNetworkBestEffort` | endpoint DNS 결과 IP를 근사 차단 | async DNS + WFP |

`ResolvedDomainNetworkBestEffort`는 exact FQDN/URL block이 아니다. 공유 CDN IP의 무관한
사이트를 막을 수 있고, 다른 resolver/DoH/VPN/proxy/DNS rotation은 우회하거나 결과를
달리할 수 있다. HTTPS path/query를 모든 앱에서 식별한다는 보장은 제공하지 않는다.

## 2. 지원 범위

### 기본 지원

- Windows의 관리형 Google Chrome
- Windows의 관리형 Microsoft Edge
- WFP가 지원되는 Windows의 IPv4/IPv6 outbound connect

### capability-gated 또는 별도 adapter

- Firefox enterprise policy
- Windows Firewall FQDN dynamic keyword
- 중앙 GPO/MDM policy publisher
- 조직 secure web gateway/managed DNS

설치돼 있지 않거나 관리되지 않는 browser, portable Chromium, embedded WebView는 지원으로
간주하지 않는다. required browser set 중 하나라도 capability가 없으면
desired state를 기록하기 전에 `Rejected(UnsupportedBrowser)`해야 한다. optional browser나
best-effort rule만 실패한 경우에 한해 `Degraded`가 될 수 있다.

## 3. 컴포넌트

```text
NetworkEnforcer
  WebPolicyCompiler
  BrowserInventory
  BrowserPolicyCoordinator
    ChromePolicyAdapter
    EdgePolicyAdapter
    optional Firefox/CentralManagementAdapter
  BrowserOwnershipStore
  AsyncDomainResolver
  WfpPolicyAdapter
  NetworkPolicyMetrics
```

NetworkEnforcer는 machine singleton이다. per-WTS-session server가 HKLM 또는 WFP를 직접
변경하지 않는다.

## 4. 정책 모델

```text
ManagedBrowserDomain
  ruleId
  fqdn
  subdomains         ExactOnly | IncludeSubdomains
  browsers[]         Chrome | Edge | ...
  profileScope       AllEnumeratedProfiles | ExplicitProfileClasses[]
  evidence           WrittenVerified | BrowserObservedRequired
  required

BrowserUrlPattern
  ruleId
  vendorPattern
  browsers[]
  profileScope       AllEnumeratedProfiles | ExplicitProfileClasses[]
  evidence           WrittenVerified | BrowserObservedRequired
  required

NetworkCidr
  ruleId
  address
  prefixLength
  protocol           Any | TCP | UDP
  remotePorts[]      optional
  required

ResolvedDomainNetworkBestEffort
  ruleId
  fqdn
  protocol/ports     optional
  staleAddressTtl
  collateralRiskAcknowledged
  required           MUST be false in first production release
```

Rule type은 UI 입력란 또는 payload schema로 명시한다. `://`나 `/`의 존재로 type을
추론하지 않는다.

`BrowserProfileClass`는 최소 `Regular`, `PrivateOrInPrivate`, `Guest`,
`ConsumerSignedIn`, `EnterpriseManaged`를 구분한다. Capability 응답은 browser/version별로
각 class의 `Covered | Unsupported | Unknown`과 근거 adapter version을 반환한다.
`AllEnumeratedProfiles`는 target이 광고한 capability schema version에 정의되어 있고 현재
inventory가 발견한 class 전체를 뜻하며 미래 vendor profile까지 보장하지 않는다. required
scope에 `Unsupported`나 `Unknown`이 하나라도 있으면 admission에서
`Rejected(UnsupportedProfileScope)`한다.

적용 뒤 새 profile class나 기존 class의 `Unknown` drift를 발견하면 `CapabilitiesChanged`를
보내고 재-admission 전까지 새 `Applied`를 금지한다. required scope면 owned artifact는
유지한 채 `Failed(ProfileScopeDrift)`, optional scope면 `Degraded(ProfileScopeDrift)`다.
capability/inventory를 갱신한 새 revision이 admission을 통과해야 정상 상태로 돌아간다.

## 5. 정규화와 문법

### 5.1 Domain

1. Unicode trim과 control/NUL 거부
2. IDNA A-label 변환
3. ASCII lowercase
4. 마지막 root dot 제거
5. label/전체 길이와 빈 label 검증
6. wildcard, port, scheme, path가 domain-only type에 섞이면 거부

`ExactOnly`와 `IncludeSubdomains`는 vendor pattern으로 명시적으로 compile한다. Chrome/Edge
형식에서 일반 `example.com`은 하위 domain도 match하고, 앞의 점 `.example.com`은 exact
host 의미를 가진다. 이 차이를 UI preview에 표시한다.

### 5.2 Browser URL pattern

Chrome/Edge 공식 형식은 다음과 같다.

```text
[scheme://][.]host[:port][/path][@query]
```

- scheme과 host는 case-insensitive다.
- path와 query case는 보존한다.
- fragment는 정책 의미에서 제거/무시되는 vendor 규칙을 따른다.
- unsupported custom scheme, invalid port, malformed host를 저장하지 않는다.
- `https://example.com/*` 같은 잘못된 trailing wildcard를 자동 보정하지 않고 오류와
  공식 형식 예시를 보여준다.

근거:

- [Chrome URL blocklist filter format](https://support.google.com/chrome/a/answer/9942583)
- [Microsoft Edge URL filter format](https://learn.microsoft.com/en-us/deployedge/edge-learnmmore-url-list-filter-format)

Chrome와 Edge parser가 현재 같아 보여도 adapter별 validator를 유지한다. vendor 변경을
공통 parser 하나가 조용히 오해하지 않게 하기 위함이다.

### 5.3 Network address

- IPv4/IPv6 literal과 prefix length를 canonicalize한다.
- port는 1~65535, range 개수에 quota를 둔다.
- hostname을 `NetworkCidr`로 자동 DNS 변환하지 않는다.
- loopback, multicast, unspecified, link-local 등의 허용/거부를 rule 결과에 명시한다.

## 6. Managed browser policy

### 6.1 Vendor semantics

Chrome/Edge `URLBlocklist`는 list policy이며 Windows registry에서는 숫자 이름의 `REG_SZ`
값으로 저장된다. 두 vendor 모두 최대 1,000개 항목 제한을 문서화하고, Allowlist가
Blocklist보다 우선할 수 있다. Edge는 dynamic policy refresh를 지원하지만 진행 중 동작과
동적 page update에는 제한이 있다.

- [Chrome URLBlocklist](https://chromeenterprise.google/policies/url-blocklist/)
- [Chrome URLAllowlist](https://chromeenterprise.google/policies/url-allowlist/)
- [Edge URLBlocklist](https://learn.microsoft.com/en-us/deployedge/microsoft-edge-policies/urlblocklist)
- [Edge URLAllowlist](https://learn.microsoft.com/en-us/deployedge/microsoft-edge-policies/urlallowlist)

Registry read-back 성공은 browser가 기존 tab/요청까지 차단했다는 증거가 아니다. Backend
result는 최소 `WrittenVerified`와 `BrowserObserved`를 구분한다.

Browser backend의 `Applied(WrittenVerified)`는 대상인 모든 지원·관리 browser class에 대해
필수 policy artifact가 compile, write, read-back됐고 탐지된 Allowlist conflict가 없다는
뜻이다. 이미 열린 tab, 진행 중 요청 또는 실제 navigation 차단을 뜻하지 않는다.
`BrowserObserved`는 별도 probe/telemetry가 실제 navigation 거부를 관측했을 때만 추가한다.
rule이 `BrowserObservedRequired`를 요구하는데 target adapter가 이를 검증할 수 없으면
적용 후 downgrade하지 말고 admission에서 `Rejected(UnsupportedEvidence)`한다.

### 6.2 소유권 불변식

Adapter MUST NOT:

- `RegDeleteTreeW()`로 `URLBlocklist` 또는 `URLAllowlist` 전체 key를 삭제한다.
- foreign value를 renumber, overwrite, clear한다.
- GPO/MDM drift를 CHECK NODE의 과거 snapshot으로 통째로 덮어쓴다.
- 같은 pattern이 이미 foreign value로 존재할 때 그 value를 자기 소유라고 기록한다.

Ownership record:

```text
BrowserArtifact
  browser
  registryView
  keyPath
  valueName
  expectedType
  expectedValueHash
  ruleId
  assignment/effective hash
  createdAt
```

삭제는 `(keyPath, valueName, type, valueHash)`가 manifest와 모두 같을 때만 허용한다.
불일치하면 남겨 두고 `ExternalPolicyConflict`를 반환한다.

### 6.3 Apply 알고리즘

1. Chrome/Edge inventory, registry view, policy authority, key ACL을 probe한다.
2. Blocklist와 Allowlist의 모든 foreign/owned value를 읽어 snapshot hash를 만든다.
3. foreign Allowlist가 required block을 무력화하는지 vendor specificity 규칙으로 검사한다.
4. 1,000개 총 entry 상한과 비어 있는 numeric value name을 preflight한다.
5. 새 value 이름을 확정하고 각 `(keyPath, valueName, type, valueHash)`, 이전 owned value,
   foreign snapshot hash, preimage, mutation 순서, rollback token을 포함한 exact intent를 만든다.
6. broker가 operation-intent WAL을 fsync하고 `IntentDurable`을 반환한다. Adapter는 이 응답
   전에 registry를 변경해서는 안 된다.
7. 새 owned value를 예약한 숫자 이름에 먼저 추가한다.
8. 각 value를 type/value로 read-back verify한다.
9. 새 set이 검증된 후 old owned value를 정확 일치 조건으로 제거한다.
10. key ACL과 foreign values가 전후 동일한지 다시 검사한다.
11. ownership delta와 managed applied hash를 journal에 commit하고 intent를 완료 표시한다.

중간 실패 시 새로 추가해 소유가 입증된 value만 반대 순서로 제거한다. concurrent GPO
변경을 발견하면 full snapshot을 복원해 GPO 변경까지 되돌리지 않고 operation을 중단한다.

Chrome 성공 후 Edge 실패처럼 browser 사이의 완전 원자성은 registry가 제공하지 않는다.
Coordinator는 역순 compensating rollback을 시도하고, rollback까지 실패하면
`RecoveryRequired`로 보고한다.

### 6.4 Clear 알고리즘

- 새 effective policy에 남은 rule을 먼저 compile한다.
- 제거 대상은 manifest의 exact owned value만이다.
- exact delete set, 현재 값 preimage, rollback token을 operation-intent WAL에 먼저 fsync한 뒤
  제거한다.
- foreign duplicate가 있어도 삭제하지 않는다.
- key가 비어도 key/ACL을 임의 삭제하지 않는다.
- read-back에서 owned block이 남으면 `Failed(ClearIncomplete)`이며 inactive로 보고하지 않는다.
- 모든 owned value가 제거되면 foreign duplicate가 계속 같은 pattern을 차단하더라도
  `Applied(InactiveOwned)`다. 이때 `ExternallyBlocked`와 `externalObservedHash`를 별도로
  보고하며 foreign 상태를 managed applied hash에 섞지 않는다.

### 6.5 외부 정책 authority

지속적으로 GPO/MDM이 key를 관리하는 환경에서는 local registry adapter와 외부 writer가
경쟁할 수 있다. BrowserInventory가 이를 탐지하면:

- 중앙 policy adapter가 있으면 그 authority를 통해 배포한다.
- 없으면 local write를 required 보장으로 승인하지 않고 `ExternalAuthority`를 반환한다.
- foreign policy를 비활성화하거나 삭제해 CHECK NODE가 ownership을 빼앗지 않는다.

## 7. WFP network backend

### 7.1 보장

WFP `NetworkCidr`는 명시한 remote IP/CIDR/port의 **새 outbound authorization**을 막는다.
이미 성립한 TCP/QUIC/UDP flow를 자동 종료한다고 주장하지 않는다.

`ResolvedDomainNetworkBestEffort`는 resolver가 현재 반환한 address set에 같은 필터를
만들 뿐 domain과 flow를 암호학적으로 결속하지 않는다.

### 7.2 Object 구조

- service lifetime의 dynamic WFP session
- CHECK NODE 전용 provider와 sublayer
- rule ID/generation/address에서 결정론적으로 만든 filter key
- IPv4 `ALE_AUTH_CONNECT_V4`, IPv6 `ALE_AUTH_CONNECT_V6`
- filter artifact manifest와 observed filter IDs

Microsoft는 crash cleanup을 위해 dynamic session, 관련 add/delete를 묶기 위한 explicit
transaction, 전용 provider/sublayer 사용을 권장한다.
[Microsoft WFP best practices](https://learn.microsoft.com/en-us/windows/win32/fwp/best-practices)

Dynamic session은 service/enforcer crash 시 필터가 사라지는 것이 정상이다. journal에서
재생성하기 전까지 잠깐 fail-open일 수 있다. service downtime 중에도 지속되는 durable
network prevention이 요구되면 별도 persistent firewall/application-control 설계가 필요하며
현재 dynamic WFP backend로 충족했다고 표시하지 않는다.

### 7.3 원자 교체

1. 기존 engine/session과 generation을 유지한다.
2. 새 filter set 전체를 memory에서 build하고 quota를 검사한다.
3. `FwpmTransactionBegin0`을 시작한다.
4. 새 generation filter를 모두 add한다.
5. add가 하나라도 실패하면 transaction abort하고 old generation을 유지한다.
6. 모두 성공하면 transaction 안에서 old generation을 delete한다.
7. commit 후 probe하여 filter count/key/hash를 확인한다.
8. journal의 applied generation을 갱신한다.

old filter를 먼저 clear한 뒤 engine/sublayer/filter를 만드는 순서는 금지한다. transaction
timeout은 명시적으로 처리하고 무한 retry하지 않는다.

## 8. Async DNS best-effort backend

### 8.1 Resolver

- DNS는 gateway/broker network thread에서 동기 실행하지 않는다.
- cancel 가능한 bounded async resolver를 사용한다.
- A/AAAA, TTL, CNAME chain, resolver identity, query time, error를 rule별로 저장한다.
- 동시 query와 address 개수에 quota를 둔다.
- policy generation이 바뀐 stale result는 폐기한다.

### 8.2 Last-known-good

- 첫 resolution이 전부 실패하면 그 rule은 `Degraded/Unapplied`; 다른 rule의 상태를 지우지
  않는다.
- 갱신 실패 시 해당 rule의 last-known-good address만 `staleAddressTtl` 동안 유지한다.
- stale 만료 뒤 제거할 때 master에 rule별 상태를 보고한다.
- 한 domain의 성공 때문에 다른 domain의 old filter를 제거하지 않는다.
- 새 address generation을 transaction commit한 뒤 old address를 제거한다.

### 8.3 명시할 우회와 collateral risk

- CDN/shared hosting IP의 무관한 domain 오차단
- browser DoH와 OS resolver 결과 차이
- VPN/proxy가 remote endpoint를 바꾸는 경우
- DNS rotation/GeoDNS/CNAME과 TTL 사이 gap
- 직접 IP 접속, hosts override, 기존 connection
- QUIC/HTTP3와 IPv4/IPv6 dual stack

`collateralRiskAcknowledged=false`면 UI는 resolved-IP rule 배포를 막는다.

## 9. 더 강한 domain/URL 제어

권장 우선순위:

1. 관리형 Chrome/Edge URL policy
2. 필요한 경우 중앙 GPO/MDM 또는 강제 설치 browser extension
3. 조직 secure web gateway/managed DNS
4. 명시적 IP/CIDR용 user-mode WFP
5. capability와 운영 전제가 충족된 Windows Firewall FQDN 기능

커스텀 WFP callout kernel driver와 TLS interception은 기본 계획에서 제외한다. HTTPS path를
모든 앱에서 보려면 인증서 배포, 개인정보/법적 검토, QUIC/ECH 대응, driver 보안과 장애
비용까지 별도 프로젝트가 된다.

지원되지 않는 browser 우회를 막아야 한다면 Process policy로 허용 browser만 실행하게 하는
구조를 검토할 수 있으나, 이는 웹 backend가 아니라 별도 application-control 정책이다.

## 10. Apply/clear/reconcile

### Apply

1. typed parser와 vendor validator를 모두 통과한다.
2. BrowserInventory와 foreign policy/Allowlist conflict를 검사한다.
3. DNS work와 WFP/browser 변경을 prepare한다.
4. 모든 backend의 exact artifact identity, preimage, commit order, rollback token을 하나의
   operation-intent WAL에 fsync한다. 어떠한 WFP/registry mutation도 그 전에 시작하지 않는다.
5. 새 WFP generation을 transaction으로 stage/commit한다.
6. browser owned values를 additive write/read-back/old-owned delete로 적용한다.
7. 두 backend를 probe한다.
8. ownership manifest와 backend result를 broker journal에 기록하고 intent를 완료 표시한다.
9. 뒤의 required backend가 실패하면 이미 commit한 reversible backend를 역순 rollback한다.
   probe가 이전 last-known-good를 확인할 때만 `Failed`, 아니면 `RecoveryRequired`다.

WFP와 여러 browser registry를 하나의 OS transaction으로 묶을 수 없으므로 완전 ACID를
주장하지 않는다. prepare를 먼저 끝내고, reversible artifact를 사용하며, rollback 실패를
숨기지 않는 것이 수용 기준이다.

### Clear

- 다른 assignment의 effective rule을 보존한다.
- owned browser value와 WFP filter만 제거한다.
- foreign Allowlist/Blocklist/key/ACL을 그대로 둔다.
- 실제 probe 전 inactive로 표시하지 않는다.

### Reconcile

- browser registry drift, ownership mismatch, WFP generation/filter count, DNS stale 상태를
  비교한다.
- owned artifact만 desired로 복원한다.
- external authority conflict는 자동 덮어쓰기 대신 `ExternalPolicyConflict`로 보고한다.

## 11. Result와 telemetry

Backend result 최소 필드:

```text
backend, browser/version/profile capability,
requestedProfileClasses, coveredProfileClasses, unknownProfileClasses,
desiredRuleCount, compiledEntryCount, ownedEntryCount, foreignEntryCount,
allowlistConflictCount, registryWrite/readBack state,
wfpGeneration, activeFilterCount, dnsResolved/failed/stale counts,
managedAppliedHash, externalObservedHash, evidence,
state, warnings, Win32/WFP errors, latency, lastProbeAt
```

기본 로그에는 전체 URL path/query를 쓰지 않는다. rule ID, host의 keyed hash, count, error
code만 남긴다. 사용자에게 보여 줄 오류도 query/token 같은 민감 값을 마스킹한다.

## 12. 필수 시험

### Parser/unit

- IDN, trailing dot, mixed case, exact/subdomain
- wildcard, custom scheme, port, path/query case, fragment, malformed input
- IPv4/IPv6/CIDR와 port range
- vendor pattern golden tests

### Browser integration

- Chrome/Edge 정상·시크릿/InPrivate와 지원 profile 종류
- Regular/Guest/ConsumerSignedIn/EnterpriseManaged scope의 capability와 required reject
- 적용 뒤 새/Unknown profile class 발견, `CapabilitiesChanged`, required/optional drift 상태
- bare domain, exact domain, subdomain, path/query
- 기존 tab, 새 navigation, XHR/fetch, service worker, WebSocket
- vendor 1,000-entry 경계
- foreign Blocklist/Allowlist와 ACL을 미리 심은 Apply/Update/Clear
- operation-intent fsync 직후와 각 registry mutation 직후 process kill
- Chrome 성공/Edge 실패, concurrent GPO drift, rollback 중 kill
- clear 뒤 foreign state byte-for-byte 보존
- foreign duplicate가 남은 clear의 `InactiveOwned`/`ExternallyBlocked` 분리

### WFP/DNS integration

- IPv4/IPv6, TCP/UDP/QUIC, 기존 connection과 새 connection
- WFP add N번째 실패와 transaction abort
- update 100회 중 허용 gap 없음
- DNS 일부/전체 실패, CNAME, TTL rotation, stale expiry
- shared CDN IP collateral test와 경고
- resolver와 browser DoH 결과 불일치
- enforcer kill 시 dynamic cleanup, restart reconcile

### Acceptance Gate

- `RegDeleteTreeW()` 없이 owned value만 제거한다.
- foreign policy와 ACL이 Apply/Update/Clear/Crash 전후 보존된다.
- WFP update 실패 시 old generation 전체가 유지된다.
- exact browser rule과 best-effort resolved-IP rule을 UI/ACK가 구분한다.
- `WrittenVerified`와 실제 `BrowserObserved`를 UI/ACK가 구분한다.
- requested profile class가 모두 covered되지 않으면 `Applied`로 표시하지 않는다.
- unsupported browser/profile/foreign Allowlist conflict를 성공으로 표시하지 않는다.
- service/server/WTS session 변화가 writer 수나 browser ownership을 바꾸지 않는다.
- 최소 Chrome와 Edge의 현재 지원 Windows 버전에서 실제 navigation 시험을 기록한다.
