# TDS 1.0.3 — AE 엔진 기본 ESL 지원 및 BEES 초기화 차단 수정

## 접수 증거와 원인
사용자가 제공한 AE 로그: TDS version=1.0.2 runtime=1-6-1170-0 ... 다음에 TDS_INIT status=BEES_MODULE_MISSING required_for=ESPFE_1_71.
첫 줄은 DLL이 로더 버전 검사를 통과해 실행됐다는 증거다. 두 번째 줄은 DataLoaded의 BEES 무조건 필수 검사에서 TDS가 return하여 기능 초기화를 중단한 증거다. 사용자 설치 누락으로 판정하지 않는다. 1.0.2에서 로더·전투 훅을 수정하면서 DataLoaded에 남은 별도 초기화 조건을 놓쳤다. 기존 정적 검사가 전체 초기화 성공을 입증하지 못했다.

## 수정
- RuntimePolicy에 엔진 기본 확장 ESL 지원과 외부 BEES 백포트를 구분하는 정책을 추가했다.
- AE1.6.1130/1170/1179는 엔진 기본 기능을 사용하고 BEES DLL을 조회하거나 요구하지 않는다.
- 확장 ESL 기능이 없는 SE1.5.97 및 구형 AE1.6.318~659에는 기존 BEES 보호 조건을 유지한다. 이 엔진들의 ESPFE1.71 처리 능력을 새로 구현한 것은 아니다.
- onMessage의 상태를 beesLoaded에서 extendedEslReady로 분리했다. 현대 AE에서 DataLoaded만 통과하고 이후 저장·로드 메시지가 계속 차단되는 오류도 방지한다.
- 정상 로그: TDS_INIT status=ESL_SUPPORT_READY source=NATIVE_ENGINE runtime=1-6-1170-0. 구형 엔진+BEES에서는 source=BEES. 실제 게임에서 출력된 것을 확인한 기록은 아니며 새 코드의 예상 로그다.
- 허용 런타임/SE·AE 적중 ABI/전투 수치/ESP·PEX·UI/저장형식 불변. 게임 설치 파일 중 DLL 하나만 변경한다.

## 검증
BUILD=PASS_SE_AE_MULTI_TARGET. 파일/제품/내보내기 메타데이터 버전1.0.3.
RuntimePolicy 호스트110검사 PASS: 모든 허용 버전의 BEES 있음/없음, 현대AE 기본 지원, 구형 미지원 거절, 미승인 버전 거절, 기존 ABI 호출 위치 선택.
기존 비용·HUD 세션 및 근접/봉헌4131, C-R2 222, 기본339, 무기출처13, 장비수령255, 조회계약22 회귀 PASS.
PE64 내보내기·런타임 목록·NG 플래그·버전 확인 PASS. 전체30파일/핫픽스1파일 ZIP 재개봉·바이트 대조 PASS. 기존1.0.2 ZIP/MO2 폴더/프로필 보존 PASS.
SOURCE=COMPLETE_NATIVE_ESL_INIT_FIX / STATIC=PASS_SOURCE_SDK_PE_HOST / DEPLOY=PASS_NEW_FOLDER_AND_TWO_ZIPS.
RUNTIME=NOT_RUN_SE_AND_AE / USER_QA=PENDING / LOCK=NO. 새 로그는 사용자의 AE 환경에서 재검증해야 한다.

## 설치
전체: release/versions/1.0.3/TULL_Dibella_says_1.0.3.zip
핫픽스: release/hotfixes/1.0.3/TULL_Dibella_says_1.0.3_Hotfix_for_1.0.x.zip
전체 설치본으로 교체하거나 기존1.0.x 위에 DLL 핫픽스를 덮어쓴다. 두 방식은 같은 DLL이다. 신규 MO2 폴더 TULL Dibella says - 1.0.3에 별도 배치했으며 자동 활성화하지 않았다. 기존1.0.2 자료는 실패 이력으로 보존한다.

## 실게임 확인 질문3개
1. AE1.6.1170에서 BEES 없이 source=NATIVE_ENGINE 이후 폼/훅 초기화가 이어지는가?
2. 메뉴·HUD·장비·전투 효과가 실제 동작하고 저장 전환 뒤에도 유지되는가?
3. SE1.5.97의 BEES 경로가 유지되며 기존 기능에 회귀가 없는가?

## 근거와 교훈
BEES 제작자 안내: 1.6.1130 이상에는 설치하지 말라고 명시하며 엔진 자체 기능과 중복된다. https://www.nexusmods.com/skyrimspecialedition/mods/106441
로더 허용, 엔진 기능의 기본 지원, 플러그인 모듈 존재, 초기화 후 메시지 처리는 서로 다른 조건이다. 버전 허용 검사의 PASS를 전체 초기화 PASS로 간주하지 않는다. 이번 회귀 시험은 실제 onMessage에서 사용하는 동일 정책을 BEES 없는 현대AE 입력으로 실행한다.

전체 ZIP SHA256=0d47966f052ec160e2398c85335252faa737fb083b5b9e7e39c02a346a21ae1a
핫픽스 ZIP SHA256=152805dc07f72b8974728af9ed4a0046ed11a46f9425244fa120dfcbab2f68ce
DLL SHA256=4a6ebb2cc029b6319367230c1f20392e89e139a162a50fad8611bebcb3afb53e
