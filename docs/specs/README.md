# CHECK NODE AccessBlock 팀 공유 가이드

상태: **차기 구조 명세 승인 / 구현 전**
기준일: 2026-09-11

팀원에게는 이 파일 하나를 시작점으로 공유한다. 모든 문서를 처음부터 읽을 필요는 없다.

> 공유 문구: `docs/specs/README.md`부터 읽고, 아래 역할별 경로에서 자기 담당 문서만
> 추가로 확인하세요. `docs/team/P1~P3`는 과거 구현 지시서이며 차기 구조의 기준이 아닙니다.

## 1. 1분 요약

현재 코드는 세션별 `veyon-server`가 브라우저 정책, WFP, 프로세스 감시를 직접 실행한다.
차기 구조에서는 server가 인증·인가 gateway만 담당하고, 머신 전역 정책은
`veyon-service`의 단일 broker가 소유한다.

```text
Master control plane
  -> per-WTS-session veyon-server (인증·인가 gateway)
  -> machine-singleton AccessBlockBroker in veyon-service
  -> NetworkEnforcer / ProcessEnforcer
  <- Accepted / Applied / Degraded / Failed / Rejected
```

현재 구현과 차기 명세를 혼동하면 안 된다.

- **현재 구현:** `QStringList` 두 개, Mode Start/Stop, server 직접 적용, 적용 ACK 없음
- **차기 목표:** typed policy, 독립 Apply/Clear/Status, singleton broker, revision/lease/ACK,
  WAL/rollback/reconcile
- **현재 승인 범위:** 문서 명세만 승인됨
- **미승인 범위:** C++ 마이그레이션, Windows 빌드, 실기기 시험, 운영 배포

## 2. 무엇을 읽어야 하나

### 모든 팀원 — 약 10분

1. 이 문서 전체
2. 루트 [README.md](../../README.md)의 **아키텍처**와 **구현 상태**
3. [ACCESSBLOCK_ARCHITECTURE_SPEC.md](ACCESSBLOCK_ARCHITECTURE_SPEC.md)의
   **3. 규범적 목표 구조**, **7~9. Apply/Clear/복구**, **13. 마이그레이션**

### 리드·통합·core 담당

- 주 문서: [ACCESSBLOCK_ARCHITECTURE_SPEC.md](ACCESSBLOCK_ARCHITECTURE_SPEC.md)
- 집중 구간: 정책 모델, FeatureMessage v2, broker 상태 머신, IPC/권한, journal,
  마이그레이션과 전체 Gate
- 관련 현재 코드:
  [AccessBlockFeaturePlugin.cpp](../../plugins/accessblock/AccessBlockFeaturePlugin.cpp),
  [WindowsServiceCore.cpp](../../plugins/platform/windows/WindowsServiceCore.cpp),
  [FeatureMessage.h](../../core/src/FeatureMessage.h),
  [VeyonServerInterface.h](../../core/src/VeyonServerInterface.h)

### 프로세스 제어 담당

- 먼저: 전체 명세의 목표 구조와 Apply 순서
- 주 문서: [PROCESS_CONTROL_SPEC.md](PROCESS_CONTROL_SPEC.md)
- 집중 구간: `Prevent/Contain/Observe`, identity `allOf`, 보호 대상, PID 재검증,
  observer+snapshot, SLO와 Acceptance Gate
- 관련 현재 코드:
  [ProcessBlocker.cpp](../../plugins/accessblock/ProcessBlocker.cpp),
  [ProcessBlocker.h](../../plugins/accessblock/ProcessBlocker.h)

### 웹·네트워크 제어 담당

- 먼저: 전체 명세의 목표 구조와 Apply/WAL 순서
- 주 문서: [WEB_CONTROL_SPEC.md](WEB_CONTROL_SPEC.md)
- 집중 구간: rule type, browser profile capability, registry value 소유권,
  WFP transaction, async DNS, rollback과 Acceptance Gate
- 관련 현재 코드: [UrlBlocker.cpp](../../plugins/accessblock/UrlBlocker.cpp)

### 설계 근거를 검토하는 사람

- [GAN_ARCHITECTURE_REVIEW.md](GAN_ARCHITECTURE_REVIEW.md)만 읽는다.
- 이 문서는 왜 기존 구조를 반려했는지와 Generator–Discriminator 반례를 기록한
  **결정 기록**이다. 구현 계약은 상세 명세가 우선한다.

## 3. 문서 우선순위

문서가 서로 다르게 보이면 다음 순서로 해석한다.

| 우선순위 | 자료 | 의미 |
| --- | --- | --- |
| 1 | 현재 C++ 소스 | 지금 실제로 동작하는 방식의 근거 |
| 2 | `docs/specs/*_SPEC.md` | 앞으로 구현할 규범적 계약 |
| 3 | 이 파일과 루트 `README.md` | 팀 공유용 요약·현재 상태 |
| 4 | `GAN_ARCHITECTURE_REVIEW.md` | 선택 이유와 반려한 대안 |
| 5 | `docs/team/P1~P3` | 과거 역할 분담·초기 구현 참고 자료 |

소스가 차기 명세와 다르다고 해서 명세가 틀린 것은 아니다. 소스는 **현재 상태**, 명세는
**목표 상태**를 설명한다. 반대로 현재 동작을 확인할 때는 명세가 아니라 소스를 기준으로 한다.

[P1-web-blocking.CLAUDE.md](../team/P1-web-blocking.CLAUDE.md),
[P2-process-blocking.CLAUDE.md](../team/P2-process-blocking.CLAUDE.md),
[P3-gui-integration.CLAUDE.md](../team/P3-gui-integration.CLAUDE.md)는 삭제하지 않았지만
신규 구현 계약으로 사용하지 않는다.
빌드 플래그나 과거 파일 소유 관계를 찾을 때만 참고하고, 충돌하면 `docs/specs/`가 우선한다.

## 4. 구조가 어떻게 바뀌었나

| 영역 | 현재/기존 구조 | 차기 구조 |
| --- | --- | --- |
| 정책 authority | WTS 세션별 server | `veyon-service`의 machine-singleton broker |
| server 책임 | 인증과 머신 전역 OS 적용 | 인증·인가·제한·reply correlation만 수행 |
| 조작 계약 | Veyon Mode Start/Stop | 독립 `Apply/Clear/Status` |
| payload | URL/app `QStringList` 두 개 | versioned typed `PolicySnapshot` |
| 상태 | 전송 직후 성공 취급 | `Accepted/Applied/Degraded/Failed/Rejected` |
| 동시 제어 | 마지막 메시지가 사실상 덮어씀 | principal+assignment+revision, 살아 있는 deny 합집합 |
| 지속성 | backend마다 서로 다른 수명 | Lease/Durable, desired/applied journal, boot reconcile |
| 부분 실패 | 이전 상태 소실 가능 | mutation 전 intent WAL, 역순 rollback, LKG probe |
| browser registry | key 전체 삭제 가능 | CHECK NODE 소유 value만 추가·검증·삭제 |
| WFP | DNS 결과 IP를 비원자 교체 | typed CIDR/best-effort 구분, generation transaction |
| 프로세스 | basename 1초 polling 후 종료 | `Prevent/Contain/Observe`, 강한 identity와 event observer |

## 5. 파일별 역할

| 파일 | 용도 | 수정할 사람 |
| --- | --- | --- |
| [README.md](../../README.md) | 제품 소개와 현재 구현 상태 | 리드 |
| [docs/specs/README.md](README.md) | 팀 공유 진입점과 읽기 지도 | 리드 |
| [ACCESSBLOCK_ARCHITECTURE_SPEC.md](ACCESSBLOCK_ARCHITECTURE_SPEC.md) | 공통 정책·wire·broker·복구 계약 | 리드/core |
| [PROCESS_CONTROL_SPEC.md](PROCESS_CONTROL_SPEC.md) | 프로세스 backend 규범 | 프로세스 담당+리드 리뷰 |
| [WEB_CONTROL_SPEC.md](WEB_CONTROL_SPEC.md) | browser/WFP/DNS backend 규범 | 웹 담당+리드 리뷰 |
| [GAN_ARCHITECTURE_REVIEW.md](GAN_ARCHITECTURE_REVIEW.md) | 설계 결정 근거 | 원칙적으로 기록 보존 |
| `docs/team/P1~P3` | 과거 구현 지시서 | 신규 계약 수정 금지 |

공통 wire, 상태 의미, 소유권, journal, 보안 경계가 바뀌면 전체 명세를 먼저 수정하고 두
상세 명세를 함께 검토한다. backend 내부 구현만 바뀌면 해당 상세 명세와 Gate를 수정한다.
GAN 기록은 새 반례로 기존 결정을 뒤집을 때만 추가한다.

## 6. 이번 작업에서 실제로 바뀐 파일

### 명세 작업으로 수정·추가

- 루트 `README.md`: 잘못된 `master -> server -> worker` 설명과 “차단 로직 미구현” 표기를
  현재 코드 기준으로 교정하고, 차기 singleton broker 구조를 안내했다.
- `docs/specs/ACCESSBLOCK_ARCHITECTURE_SPEC.md`: 전체 목표 구조, v2 wire, 정책 합성,
  WAL/rollback/recovery, 마이그레이션 Gate를 새로 정의했다.
- `docs/specs/PROCESS_CONTROL_SPEC.md`: 실행 예방과 사후 containment를 분리하고 identity,
  보호 규칙, action, SLO, 시험 계약을 정의했다.
- `docs/specs/WEB_CONTROL_SPEC.md`: browser URL policy, WFP CIDR, DNS-IP best-effort를
  분리하고 외부 정책 보존과 원자 교체 계약을 정의했다.
- `docs/specs/GAN_ARCHITECTURE_REVIEW.md`: 기존 명세 반례와 최종 선택 이유를 기록했다.
- 이 파일: 위 문서를 찾기 위한 단일 진입점으로 재구성했다.

### 작업 트리에 함께 남아 있는 기존 C++ 수정

다음 세 파일의 미커밋 변경은 명세 작업 전에 존재하던 **legacy 구현 보강**이며 차기
broker 구조 구현이 아니다.

- `ProcessBlocker.cpp`: 실행 파일 이름 case-fold 정규화, 보호 이름/self PID 검사,
  Windows handle RAII를 추가했다.
- `ProcessBlocker.h`: `QTimer`에 QObject parent를 지정했다.
- `UrlBlocker.cpp`: registry key handle RAII를 추가했다.

여전히 basename polling, server 직접 적용, `RegDeleteTreeW()`, 비원자 WFP 교체 등의
구조적 한계가 남아 있다. 이 세 변경만으로 새 명세가 구현됐다고 표시하면 안 된다.

### 현재 Git 공유 상태

- `docs/specs/` 문서들은 아직 untracked다.
- 루트 `README.md`와 위 C++ 세 파일은 modified 상태다.
- 이번 문서 정리까지 commit/push하지 않았다.

따라서 현재는 이 로컬 작업 공간에서만 보인다. 팀 저장소에 공유하려면 이후 의도한 파일만
명시적으로 stage해 commit하고, 별도 승인에 따라 push해야 한다. C++ legacy 보강과 명세
문서를 한 commit에 섞을지는 먼저 결정한다.

## 7. 구현 착수 순서

1. 현재 legacy 동작과 외부 browser policy 공존, 다중 WTS 세션을 재현하는 테스트를 고정한다.
2. v2 typed message, ACK/status, revision/hash와 인증 principal 전달을 구현한다.
3. `veyon-service`에 singleton broker와 journal을 shadow mode로 추가한다.
4. browser value 소유권과 WFP transaction을 먼저 안전하게 만든다.
5. 프로세스 identity 재검증과 event observer+snapshot reconciliation을 구현한다.
6. broker를 유일 writer로 전환하고 legacy server 직접 적용을 중단한다.
7. crash/reboot, stale/duplicate, 다중 controller, Windows 실기기 Gate를 통과한다.

세부 cutover 순서와 중단 조건은 전체 명세의 **13. 마이그레이션**을 따른다.

## 8. 팀 리뷰 체크리스트

- 내가 구현한 것이 현재 legacy 수정인지 차기 v2 구현인지 PR/커밋에 표시했는가?
- per-session server가 HKLM/WFP/process watcher를 직접 소유하지 않는가?
- 지원하지 않는 required guarantee를 성공이나 best-effort로 낮추지 않는가?
- OS mutation 전에 exact intent와 rollback 정보가 내구화되는가?
- CHECK NODE가 만들지 않은 browser/GPO/MDM artifact를 건드리지 않는가?
- 전송 성공과 실제 `Applied`를 구분하는가?
- Windows/MSYS2 build와 실기기 검증 여부를 정확히 기록했는가?

현재 문서 검증은 완료됐지만 C++ 마이그레이션과 Windows 실기기 검증은 아직 수행되지 않았다.
