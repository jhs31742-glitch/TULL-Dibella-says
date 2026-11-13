# TDS 1.0.2 — SE/AE 호환 핫픽스 및 전체 설치본

## 변경 배경과 원인
사용자는 AE 호환 구현을 요청했으나 1.0.1 배포본에는 SE1.5.97만 허용하는 acceptsRuntime 조건이 남아 있었다. Query/Load의 거부뿐 아니라 무기·주문·받는 피해 훅에도 SE 전용 조건이 있었다. AE SKSE가 로드 전에 읽는 SKSEPlugin_Version 내보내기도 없었다. ENABLE_SKYRIM_AE 빌드 옵션만 켜져 있다고 실제 지원되는 것은 아니다.

## 구현
- 1.0.2는 SE1.5.97 및 AE1.6.318/323/342/353/629/640/659/1130/1170/1179 런타임을 허용한다. 이는 코드/정적 호환 대상이며 각 버전의 실게임 PASS를 뜻하지 않는다. VR과 미확인 미래 버전은 지원하지 않는다.
- SKSEPlugin_Query/Load, 피해·주문 훅이 하나의 RuntimePolicy를 사용한다. SKSEPlugin_Version을 추가하고 CommonLib NG Address Library 및 런타임 구조 접근자를 사용하는 메타데이터를 내보낸다. POT의 StructCompatibility::Independent와 같은 선언이다.
- SE 적중 위치 ID37633+0x16A의 6인자 ScriptEventSourceHolder 호출과 AE ID38586+0xFA의 2인자 AIProcess 호출을 분리한다. AE에 SE 함수 포인터를 강제로 적용하지 않는다. 실제 E8 명령 검사는 유지한다.
- AE는 AIProcess 소유 대상, HitData.aggressor 및 sourceRef에서 공격자·투사체를 확인한 뒤 SE와 동일한 processHit 정산을 호출한다. 참조는 NiPointer로 유지한다. 원본 함수는 각 ABI에 맞춰 정확히 한 번 호출한다. 대상 미확인 시 정산 없이 원본 호출만 전달한다.
- CommonLib 선언의 PlayerCharacter 주 vtable HandleHealthDamage=0x104, ActorValueOwner=0x06, ValueModifierEffect 계열 ModifyActorValue=0x20을 SE/AE 공통으로 사용한다. 기존 실행 가능 주소/선행 모듈/재귀 차단 검사를 유지한다.
- CommonLib 실제 빌드 캐시는 SE=ON, AE=ON, VR=OFF다. Actor::AsActorValueOwner, PlayerCharacter::GetGameStatsData 등 629 전후 런타임 접근자를 유지한다.
- ESP/PEX/UI/자산/게임 수치/장비 지급/저장 형식은 1.0.1과 바이트 동일하다. 설치 내용에서 변경되는 파일은 DLL 하나뿐이다.

## 검증 결과와 한계
- 통합 Native 빌드 exit0, 파일/제품/SKSE 메타데이터 버전1.0.2.
- 59개 런타임 허용/거부 및 SE·AE 호출 위치 선택 호스트 검사 PASS.
- 기존 비용·HUD 세션 검사, 근접/봉헌4131, C-R2 222, 기본339, 공격출처13, 장비수령255, 정확한 레코드조회22 검사 PASS.
- 실제 PE64 내보내기 Query/Load/Version, Address Library/NG 플래그, 11개 런타임 목록, 1.0.2 버전 검사 PASS.
- ZIP 재개봉·파일별 바이트 대조, 전체30파일·핫픽스1파일, 설치본 대조 PASS. 1.0.1 ZIP·기존 MO2 폴더·프로필 보호 검사 PASS.
- SOURCE=COMPLETE_TARGETED_AE_ADAPTER / STATIC=PASS_SOURCE_SDK_PE_HOST / BUILD=PASS_SE_AE_MULTI_TARGET / DEPLOY=PASS_NEW_FOLDER_AND_TWO_ZIPS.
- RUNTIME=NOT_RUN_SE_AND_AE / USER_QA=PENDING / LOCK=NO. 실제 AE 게임/모드 조합·주소 라이브러리·메뉴 렌더·타격 결과는 아직 실행하지 않았다. 정적 검사와 호스트 검사를 실게임 PASS로 승격하지 않는다.

## 설치
전체 설치: release/versions/1.0.2/TULL_Dibella_says_1.0.2.zip
핫픽스: release/hotfixes/1.0.2/TULL_Dibella_says_1.0.2_Hotfix_for_1.0.x.zip
기존1.0.0/1.0.1 설치에 핫픽스를 덮어쓰거나, 기존 TDS를 끄고 전체1.0.2 하나를 사용한다. 두 방식은 같은 DLL이다. 새 MO2 폴더는 TULL Dibella says - 1.0.2이며 프로필은 자동 활성화하지 않았다.
AE에는 해당 게임 버전의 SKSE·AE용 Address Library 및 SexLab/PrismaUI 호환판이 필요하다. 원본 의존 모드를 동봉하거나 변경하지 않았다. 확장 ESPFE1.71을 구형SE/AE에서 사용하는 경우 BEES가 필요하다. 1.6.1130 이상은 엔진 자체의 확장 지원을 사용한다.
배포 압축에는 Docs/TDS_MANIFEST/README/Papyrus 소스를 넣지 않았다. 라이선스 고지는 기존 SKSE/Plugins/TDS_NOTICES.txt를 유지했다. 근접 추가 고정 피해·기본 자원·통계·노드 정책 변경은 없다.

## 사용자 QA 핵심 3항목
1. AE에서 SKSE가 DLL1.0.2를 로드하고 메뉴/HUD·통계·장비 지급이 정상 작동하는가?
2. 근접/원거리 내성·파괴 주문·받는 피해/주문비용 보정 및 POT 동시 활성 시 정상 작동하는가?
3. SE1.5.97의 기존 기능·저장 전환이 유지되는가? SKSE 로그와 SexLabLifetimeProgression.log의 runtime, TDS_HOOK abi, 훅 등록 실패 기록을 함께 대조한다.

## 시행착오와 교훈
첫 빌드는 size_t→ptrdiff_t 및 unsigned→uint16_t 초기화 축소 오류로 실패했다. 범위가 확정된 런타임 설정에 명시적 형변환을 적용해 재빌드했으며 첫 실패 로그를 보존했다. 기존 SE 거부문만 지우거나 빌드 옵션만 바꾸는 방식은 AE 포팅이 아니다. 로더 메타데이터·원본 함수 인자·서브객체 레이아웃·게임 효과 훅 전체를 함께 감사해야 한다.

## 참고 근거
- 로컬 POT src/Combat.cpp WeaponHitHook의 SE/AE 호출 위치와 함수 원형. POT 원본은 수정하지 않았다.
- 설치 CommonLibSSE-NG Actor.h/ActorValueOwner.h/ValueModifierEffect.h/RuntimeDataAccessors.h/Interfaces.h 및 빌드 캐시.
- https://github.com/CharmedBaryon/CommonLibSSE-NG/wiki/Runtime-Targeting
- https://github.com/doodlum/CommonLibSSE-NG/blob/main/cmake/CommonLibSSE.cmake

## 배포 SHA256
전체 ZIP: 30e38ad99b3af549afc3ca1d67f0a26cc87dadb346e69b73737054ae9ff0a216

핫픽스 ZIP: 64dd9ffa70a73a60da4bd8fbe8e3be742c4514576c9f1a0ed828851af891c105

DLL: 13f8309e8fd68bb1abdd3f7d4991cc2ff7252cd09e86016298fb7c5d24526cfd
