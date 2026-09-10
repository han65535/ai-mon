# 빌드 및 실행 검증

## 도구 고정

| 항목 | 값 |
| --- | --- |
| 도구 묶음 | LLVM-MinGW `20260908`, UCRT x86_64 |
| 컴파일러 | Clang 23.1.1 |
| C++ 표준 | C++17 |
| JSON 파서 | cJSON v1.7.19 |
| OS 대상 | Windows 10/11 x64 |
| 실제 검증 OS | Windows NT 10.0.26200.0 (Windows 11) |

공식 도구 다운로드: [LLVM-MinGW 20260908](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260908).
사용한 파일: `llvm-mingw-20260908-ucrt-x86_64.zip`.

SHA-256:

```text
1bcf74d06b724aeecaa6412ca85f5b26fb1da770e7cdcefa9263c9c5c3ad34b6
```

cJSON 소스는 [v1.7.19 태그](https://github.com/DaveGamble/cJSON/tree/v1.7.19)에서 받았으며 체크섬은 다음과 같다.

```text
cJSON.c  298581a04a36c0165da4b0aade235c23088cb2faa58651d720ea2f3706ed0b0d
cJSON.h  25b0145150d500498e4d209cec69c18c42cf818bffcc54690be3b895a2a16dee
LICENSE  a36dda207c36db5818729c54e7ad4e8b0c6fba847491ba64f372c1a2037b6d5c
```

## 빌드 구성

- `Release`: 크기 최적화, LTO, 정적 C++ 지원 라이브러리, 디버그 심볼 제외.
- `Debug`: 심볼과 최적화 없는 빌드. 배포 크기 검사 대상에서 제외.
- `Test`: 독립된 콘솔 테스트 실행 파일. 제품 EXE에 테스트 코드를 포함하지 않는다.

모든 구성에서 컴파일 경고를 오류로 취급한다. 예외와 RTTI를 유지한다.
ASLR, DEP, High Entropy VA, CFG 설정을 사용하며 Release 검사에서 주요 보호 플래그를 확인한다.
아이콘·manifest·버전 정보·cJSON 고지는 EXE 안에 포함된다.

기본 빌드 명령과 별도 도구 경로 사용법은 [README](../README.md)에 있다.
아이콘 원본을 다시 생성할 때만 `scripts/make-icon.ps1`을 실행한다. 일반 빌드는 포함된 ICO를 사용한다.

## 배포 검사

`scripts/check-release.ps1`은 다음을 확인하고 `out/Release/release-check.json`에 측정 결과와 SHA-256을 기록한다.

1. EXE 크기가 1,000,000바이트 이하인지.
2. PE가 x64 실행 파일인지.
3. DLL import가 Windows 시스템 DLL 및 UCRT API set 허용 목록에만 속하는지.
4. ASLR, DEP, CFG 플래그가 있는지.

포터블 배포 대상은 `out/Release/ai-mon.exe` 하나다. 설치형은 `scripts/package.ps1`로 만드는 단일 MSI를 배포한다.
`.tools`, Debug, Test, JSON 검사 결과는 배포하지 않는다.
프로그램은 별도 비시스템 DLL, 웹 런타임 또는 실행 파일 압축을 사용하지 않는다. MSI는 CAB 압축으로 원본 EXE를 보관한다.
설치·복구·제거 검증은 [설치 패키지 문서](INSTALLER.md)를 참고한다.

## 검증 기록: 2026-09-10

### 0.2.0 다국어 배포

- 한국어·영어 49개 앱 번역 키와 이름 있는 치환 변수 일치 검사.
- 기존 집계 검사와 언어팩 파싱·선택·영어 대체 표시·설정 이전을 포함한 자동 검사 150개 통과.
- 숨김 UI에서 한국어·영어 즉시 전환, 자동 선택, 재실행 시 선택 유지, 외부 언어팩 선택 및 삭제 후 영어 대체 표시 검사.
- 영문·한국어 MSI 각각 설치·복구·제거 24개 검사 및 별도 비표시 데스크톱의 설치 마법사 검사.
- 언어팩 규격은 [LOCALIZATION.md](LOCALIZATION.md), 앱 안내는 [영문 README](../README.md)와 [한국어 README](../README.ko.md)에 정리했다.

아래 실로그 집계·디버거·상주 자원 측정은 초기 MVP 검증 기록이다. 다국어 버전에서 장시간 자원 측정을 다시 수행한 것은 아니다.

- 제공자 파싱·중복 제거·누적값 차분·날짜·캐시·설정·감시 및 취소에 대한 자동 테스트 통과.
- 실제 로컬 Claude Code 및 Codex 기록을 읽고 오늘 사용량 집계 및 캐시 생성 확인.
- 초기 검증에서 약 183MB의 로그 읽기를 완료했으며 캐시 저장에 성공.
- 변경 없는 파일은 재시작 후 로그 본문을 읽지 않는 것을 자동 테스트로 확인.
- 숨김 Win32 창과 작업 스레드 생성, 설정 컨트롤 및 저장, 정상 종료를 smoke 모드로 확인.
- LLDB에서 Debug 빌드의 `wWinMain` 중단점, 호출 스택, 재개 후 정상 종료 확인.
- 상주 프로세스를 약 153초 측정한 결과 Private Bytes 약 2.4MB, Working Set 약 9.8MB, 핸들 수 110. 12개 논리 프로세서 기준 전체 CPU 평균 약 0.016%.

상주 측정 중 원본 로그에 추가 기록이 발생했다. 위 수치는 해당 PC의 짧은 측정 결과로, 계획한 5분 유휴·24시간 안정성 검증을 대체하지 않는다.
최종 EXE 크기와 해시는 재빌드마다 달라질 수 있으므로 생성된 `release-check.json`을 기준으로 한다.

## 아직 실제 검증하지 않은 항목

- 별도 개발도구와 런타임이 없는 깨끗한 Windows 10 PC에서의 실행 및 세부 최소 OS 빌드.
- 여러 DPI 배율과 모니터 이동, 고대비 화면에 대한 수동 확인.
- Explorer 강제 재시작 및 절전 복귀 시나리오.
- 24시간 연속 실행과 극단적인 로그 규모에서의 메모리 변화.
- 다른 버전의 제공자 로그, WSL·원격 경로·보관된 세션의 자동 수집.

이 버전은 로컬 사용량 MVP다. 계정 한도, 비용 추정, 그래프, 알림, 자동 실행은 후속 범위다.

## 자동화용 실행 모드

GUI를 띄우지 않고 집계 결과를 파일로 저장할 수 있다. 테스트·진단 용도이며 별도 데이터 폴더를 지정한다.

```powershell
Start-Process -FilePath '.\out\Release\ai-mon.exe' -WindowStyle Hidden -Wait -ArgumentList @(
    '--scan',
    '--data-dir', 'C:\temp\ai-mon-test',
    '--output', 'C:\temp\ai-mon-test\summary.json'
)
```

선택적으로 `--claude <폴더>`와 `--codex <폴더>`로 원본 경로를 지정한다. 부모 폴더는 미리 존재해야 한다.
`--scan`은 저장된 GUI 설정 대신 명령줄의 경로나 기본 탐색 경로를 사용한다.
동일 데이터 폴더를 사용 중인 다른 인스턴스가 있으면 종료 코드 4를 반환하여 캐시의 동시 기록을 방지한다.

`--smoke-test --data-dir <별도 폴더>`는 숨김 창과 비활성 제공자로 시작해 기본 UI·설정 저장을 확인하고 종료한다.
`--language en|ko|auto|<언어팩 ID>`는 이번 실행의 언어 선택을 지정한다. 설정 저장 시 함께 보존된다.
`scripts/test-languages.ps1`은 별도 테스트 데이터 폴더에서 언어 전환과 재시작을 검사한다.
0.2.0부터 `--scan`의 `status`는 UI 언어와 무관한 영문 코드로 출력한다. 코드 목록은 [언어팩 문서](LOCALIZATION.md)를 참고한다.
`tests/reference_scan.py`는 Python이 있을 때 선택적으로 실행하는 독립 집계 비교 도구이며, 일반 빌드와 테스트에는 Python이 필요하지 않다.
