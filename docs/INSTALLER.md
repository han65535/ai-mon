# AI Mon 설치 패키지

## 배포 파일

- 파일: `out/Installer/AI-Mon-0.3.4-x64-en.msi` (한국어: `AI-Mon-0.3.4-x64-ko.msi`)
- 형식: Windows Installer MSI, 압축 CAB 내장
- 크기: **196,608바이트** (192KiB)
- 포함된 앱: `ai-mon.exe`, 시계 높이 미니 그래프·투명도·자동 실행 선택 포함
- 설치 화면: 영어(`-en.msi`) / 한국어(`-ko.msi`), 두 파일은 같은 다국어 EXE를 포함
- 앱 언어: 설정에서 자동 감지·한국어·영어·외부 언어팩 선택
- 대상: Windows 10 이상 x64
- 설치 범위: 현재 사용자, 관리자 권한 요청 없음

두 언어 중 하나의 MSI를 선택한다. 설치 화면 언어는 앱의 언어 설정을 덮어쓰지 않는다. 같은 버전의 두 MSI는 같은 제품이므로 나란히 설치하지 않는다. 복구에는 처음 설치한 MSI를 사용한다.

설치 파일 하나로 설치가 완료되며 인터넷 연결이나 런타임 다운로드가 필요하지 않다.
MSI와 포함된 앱 모두 1,000,000바이트 제한을 만족한다. 코드 서명은 적용하지 않았다.

## 설치와 제거

MSI를 더블클릭하고 **설치**를 선택한다.

| 항목 | 위치 및 동작 |
| --- | --- |
| 실행 파일 | `%LOCALAPPDATA%\Programs\AI Mon\ai-mon.exe` |
| 시작 메뉴 | 현재 사용자의 `AI Mon\AI Mon` 바로가기 |
| 앱 등록 | Windows 설정의 설치된 앱 목록에 AI Mon 등록 |
| 설정·캐시 | 기존 `%LOCALAPPDATA%\AI Mon` 유지 |
| 복구 | 같은 MSI를 다시 열어 **복구** 선택 |
| 제거 | 같은 MSI의 **제거** 또는 Windows 설정에서 제거 |

설치가 끝난 뒤 시작 메뉴에서 AI Mon을 실행한다. 설치 프로그램은 앱을 자동 실행하거나 로그인 자동 실행을 등록하지 않는다.
업데이트·복구·제거 전에는 실행 중인 앱을 트레이의 **종료** 메뉴로 닫는다. Claude 상태줄을 연결한 경우 앱을 이동하거나 제거하기 전에 설정에서 **Claude 상태줄 연결 해제**를 누른다. MSI는 Claude 설정을 직접 수정하지 않는다.

제거는 MSI가 설치한 EXE, 바로가기와 설치 등록만 처리한다. 폴더는 비어 있을 때만 제거한다.
사용자 설정·캐시, 원본 AI 로그 및 설치 폴더에 사용자가 추가한 파일을 재귀적으로 삭제하지 않는다.

## 생성 방법

```powershell
.\scripts\package.ps1 -Language en
.\scripts\package.ps1 -Language ko
```

이미 빌드된 Release EXE의 크기와 시스템 DLL 의존성을 검사한 뒤 MSI를 만든다.
앱을 다시 빌드하려면 `-Rebuild`, 다른 LLVM-MinGW 위치를 쓰려면 `-Toolchain`을 지정한다.

패키징에는 Windows 기본 `makecab.exe`, Windows Installer COM API와 PowerShell만 사용한다.
NSIS, Inno Setup, WiX 또는 상용 도구 설치가 필요하지 않다. 시스템 PATH와 설치 정책은 변경하지 않는다.

| 파일 | 역할 |
| --- | --- |
| `installer/product.json` | 제품 계열 및 컴포넌트 식별자 |
| `installer/ui.en.json`, `installer/ui.ko.json` | 영어·한국어 설치·유지 관리·완료 화면 문구 |
| `scripts/package.ps1` | CAB와 MSI 데이터베이스 생성, 크기 검사 |
| `scripts/msi-common.ps1` | Windows Installer Automation 호출 |
| `out/Installer/installer-check.<en 또는 ko>.json` | 크기, 버전, 제품 코드, 패키지와 EXE의 SHA-256 |

앱 버전은 EXE의 버전 리소스에서 읽는다. 같은 버전에는 같은 ProductCode를 사용하고, 패키지마다 새 PackageCode를 생성한다.
버전을 올릴 때 공통 UpgradeCode로 이전 버전을 찾도록 구성했다. 기존 설치 위치는 레지스트리에서 복원하며, 더 높은 버전이 있으면 신규 설치를 차단한다.
서로 다른 버전 간 실제 업그레이드·다운그레이드 시나리오는 후속 버전 배포 전에 추가 검증해야 한다.

## 검증 결과

2026-09-10, Windows 11 빌드 26200에서 확인했다.

0.3.3: 시계 높이 미니 창과 새 설정을 포함한다. 설치는 자동 실행을 켜지 않는다. 제거 시 설치된 EXE의 `--remove-startup` 명령으로 해당 실행 파일의 자동 실행 항목만 정리하도록 했으며, 업그레이드에서는 보존한다. 파일이 누락된 경우에도 이 정리 동작 때문에 제거가 차단되지 않는다. 실제 제거·업그레이드는 이번 PC의 기존 설치를 보존하기 위해 실행하지 않았다.

0.3.2: 한국어·영어 MSI 각 196,608바이트. 미니 그래프가 포함된 동일한 EXE를 내장한다. 두 언어 설치 마법사 검사를 통과했으며 기존 설치를 교체하거나 GitHub 릴리스를 게시하지 않았다. 0.3.2 실제 설치·업그레이드·제거·복구는 별도 실행하지 않았다.

0.3.1: 한국어·영어 MSI 각 192,512바이트. 설치 마법사 검사와 기존 0.3.0 → 0.3.1 실제 사용자 설치 업그레이드 성공. 설치 EXE 해시와 앱·Claude 설정 보존을 확인했다. 업그레이드 결과는 `out/Installer/upgrade-check.0.3.1.json`에 기록했다. 해당 버전의 제거·복구는 별도 실행하지 않았다.

0.3.0에서는 기존 사용자 설치를 감지해 설치·복구·제거 검사를 중단했다. 아래 24개 설치 검증은 0.2.0의 기록이며 최신 버전의 실설치 검증으로 간주하지 않는다.

**영어·한국어 패키지 각각 설치·복구·제거 24개 검증 통과:**

- 기존 설치가 없는 것을 확인한 뒤 작업 폴더 안의 임시 경로에 설치.
- 바로가기 위치도 임시 폴더로 지정하여 실제 시작 메뉴를 변경하지 않고 확인.
- 설치된 EXE의 SHA-256이 Release와 일치하는지 확인.
- Windows Installer 등록과 현재 사용자 설치 범위 확인.
- 설치된 앱의 숨김 UI·설정 저장·정상 종료 검사.
- 설치된 EXE를 삭제한 뒤 MSI 복구로 같은 파일이 복원되는지 확인.
- 별도 경로를 재지정하지 않고 복구·제거해 기존 설치 위치 복원을 검증.
- 제거 후 EXE·바로가기·설치 등록이 없어지는지 확인.
- 사용자 설정과 설치 폴더에 추가한 무관한 파일이 보존되는지 확인.

**설치 화면 검증 통과:**

- 화면을 전환하지 않는 별도 Windows 데스크톱에서 실제 MSI 마법사를 열어 확인.
- 영어·한국어 각각의 환영 화면, 취소 버튼, 취소 완료 화면과 종료 코드 1602 검증.
- 사용자에게 설치 창을 표시하거나 이 UI 검사로 제품을 설치하지 않음.

```powershell
# 실제 설치 등록을 생성했다가 제거하는 통합 검사
# 이미 AI Mon이 설치되어 있으면 중단한다.
.\scripts\test-installer.ps1 -Language en
.\scripts\test-installer.ps1 -Language ko

# 별도 비표시 데스크톱에서 설치 화면을 열고 취소
.\scripts\test-installer-ui.ps1 -Language en
.\scripts\test-installer-ui.ps1 -Language ko
```

결과는 `out/Installer/installer-test.<en 또는 ko>.json`, 로그는 각 `out/installer-test-*` 폴더와
`out/Installer/wizard-test.<en 또는 ko>.log`에 남긴다. 테스트 파일은 배포 대상에 포함하지 않는다.
별도 Windows 10 실기기에서의 설치는 아직 확인하지 않았다.

## 자동 설치

```powershell
msiexec /i "AI-Mon-0.3.4-x64-en.msi" /qn /norestart
```

기본 설치 위치가 권장된다. 별도 위치가 필요한 배포 환경에서는 `INSTALLDIR`를 지정할 수 있다.
현재 사용자 설치만 허용하며 `ALLUSERS=1`을 전달하면 설치를 중단한다.
