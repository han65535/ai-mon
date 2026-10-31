# 리눅스 UI 크기 예비 실험

이 디렉터리는 제품 UI가 아닌 실험용 초안이다. `fltk-basic.cpp`와 `gtk.cpp`는 제공자 이름, 고정 값 막대 두 개, 입력란과 새로고침 버튼을 표시한다. 버튼에는 기능이 없고 트레이, 설정 저장, 미니 창, 접근성 및 실제 수집 기능은 구현하지 않았다. 이 실험만으로 기능이나 제품의 최종 크기를 비교할 수 없다.

## 예비 측정

Ubuntu 24.04 x86-64 컨테이너, GCC 13, GTK 4.14.5, FLTK 1.4.5 소스 빌드. FLTK는 X11과 Wayland를 모두 켜고 `FLTK_USE_LIBDECOR_GTK=OFF`, `CMAKE_BUILD_TYPE=MinSizeRel`로 빌드했다. 두 실행 파일 모두 strip을 적용했다.

| 항목 | 크기 |
| --- | ---: |
| GTK 기본 창 실행 파일, GTK 동적 링크 | 14,472바이트 |
| FLTK 기본 창 실행 파일, FLTK 동적 링크 | 14,640바이트 |
| FLTK 공유 라이브러리, strip 적용 | 1,574,512바이트 |
| FLTK 기본 창 실행 파일, FLTK 정적 링크 | 805,768바이트 |
| GTK 공유 라이브러리 파일 | 11,793,528바이트 |
| FLTK 정적 라이브러리 아카이브 | 3,387,392바이트 |

동적 링크한 두 실행 파일은 비슷하지만, 라이브러리와 전이 의존성은 포함하지 않는다. 정적 링크 결과와 동적 링크 결과도 직접 비교하면 안 된다. 특히 GTK 수치는 GTK 및 전이 의존성이 빠져 있으며, FLTK 실행 파일에도 다른 동적 의존성이 남아 있다. 시스템에 이미 있는 라이브러리의 공유 여부와 배포 방법에 따라 추가 설치 용량이 달라진다. 개발 도구 설치 용량은 제품 배포 용량이 아니다.

GTK: `g++ -Os -s gtk.cpp -o gtk-demo $(pkg-config --cflags --libs gtk4)`.
FLTK 정적 링크: 해당 구성으로 빌드한 FLTK의 `fltk-config --compile fltk-basic.cpp` 실행 후 `strip`을 적용했다. 버전과 구성에 따른 차이가 있으므로 같은 도구 설정으로 재측정해야 한다.

## 남은 검증

- 동일한 링크 방식과 최적화 조건으로 다시 빌드하고, 깨끗한 데스크톱 기준 추가 설치·다운로드 크기를 측정한다.
- 트레이 연동을 추가한 전후의 크기를 측정한다. 트레이가 없는 경우에도 창에서 종료할 수 있어야 한다.
- X11과 Wayland에서 한국어 입력, 접근성, 화면 배율과 창 조작을 확인한다.
- 미니 창의 위치 복원·투명도를 각 환경에서 확인한다.
- 실제 제품 기능과 동등한 시제품으로 메모리 및 배포 크기를 재측정한 다음 라이브러리를 결정한다.

## X11 기능 시제품

`fltk.cpp`는 X11 전용 별도 시제품이다. XEmbed 트레이 아이콘, 메뉴, 미니 창 이동, 위치 저장과 90% 불투명도 요청을 구현했다. 저장 경로는 `$XDG_STATE_HOME/ai-mon-ui-prototype/position` 또는 `~/.local/state/ai-mon-ui-prototype/position`이다. 미니 창 위치와 불투명도는 창 관리자 및 합성기에 따라 달라질 수 있다. 트레이를 사용할 수 없으면 상세 창을 유지하며 종료 버튼으로 종료할 수 있다. 한글 입력 시험을 위한 입력란이 있다. 데이터는 고정 예시다.

X11과 Wayland 백엔드를 모두 활성화한 FLTK 1.4.5의 정적 라이브러리로 컴파일하고 strip한 결과 822,152바이트였다. X11 API와 다른 라이브러리는 동적 링크되어 있어 독립 실행 파일 크기가 아니다. Ubuntu 24.04 GNOME X11 세션에서 창 생성은 `xwininfo`로 확인했으나, 실제 한글 입력과 시각적 투명도는 아직 검증하지 않았다. Wayland 네이티브 구현은 없다. 이 시제품은 FLTK 백엔드를 X11로 강제하며 Wayland에서의 동작을 입증하지 않는다.

빌드는 기존 FLTK 빌드 디렉터리에서 `fltk-config --compile fltk.cpp`를 사용한다. 배포용이 아니며 트레이 호스트 재시작과 비정상 종료 등도 추가 검증이 필요하다.

## 자동 검증 결과

Ubuntu 24.04 컨테이너의 Xvfb, Openbox, stalonetray에서 `test-x11.sh`를 실행했다. 상세 창과 미니 창 생성, 투명도 X 속성 설정, 드래그 후 위치 저장 및 재시작 복원, 트레이 아이콘 삽입과 왼쪽 클릭으로 상세 창 열기, 트레이 종료 후 창 접근, 트레이 없는 환경의 종료 버튼을 확인했다. 이 검사는 실제 합성 결과, 한국어 입력기, 트레이 우클릭 메뉴, 데스크톱별 호환성을 보장하지 않는다.

테스트 의존성은 Xvfb, xdotool, x11-utils, Openbox 및 stalonetray다. `AIMON_PROTOTYPE=/절대경로/실행파일 ./test-x11.sh`로 실행하며 가상 디스플레이 :92가 비어 있어야 한다. 테스트는 임시 HOME을 사용한다.

Wayland의 초기 조사로 Weston 13을 Xvfb 위에 중첩 실행하고 기본 창 시제품에 Wayland 백엔드를 강제했다. FLTK와 GTK 모두 xdg_toplevel 제목 설정과 wl_surface commit이 기록됐으며 강제 종료 전까지 실행됐다. 별도의 입력 장치가 없는 Weston headless 백엔드에서는 FLTK 기본 창이 시작 중 segmentation fault로 종료됐다. 원인은 확정하지 않았으며 실제 Wayland 세션의 호환성을 의미하지 않는다. FLTK X11 트레이 시제품은 X11을 강제하므로 이 Wayland 시험의 대상이 아니다.

GTK X11 비교 시제품이 추가됐지만 표시와 메뉴 구현이 완전히 같지는 않으며, 설치 용량과 메모리 비교는 완료되지 않았다. 이 결과로 최종 UI 라이브러리를 확정하지 않는다.

## GTK X11 비교와 의존성 예비 측정

`gtk-x11.cpp`는 트레이, 상세 창, 입력란, 미니 창 이동·복원, 투명도 요청, 종료를 구현한 별도 실험이다. GTK 4.14.5에서 `g++ -std=c++17 -Wall -Wextra -Os -s gtk-x11.cpp -o gtk-x11 $(pkg-config --cflags --libs gtk4 x11)`로 빌드했다. `test-gtk-x11.sh`를 같은 가상 X11 환경에서 실행하여 트레이 왼쪽 클릭, 오른쪽 메뉴 표시와 항목 선택, 트레이 소실, 미니 창 위치 복원과 종료를 확인했다. 두 시제품은 완전히 동일한 화면이나 기능이 아니므로 아래 수치는 참고용이다.

동일한 컨테이너에서 각 실행 파일의 `ldd` 결과로 발견한 공유 라이브러리의 실제 경로를 중복 제거하여 파일 크기를 합산했다. FLTK 공유 라이브러리는 이 실험에서 MinSizeRel로 빌드한 1.4.5이며, GTK는 배포판 제공 4.14.5이다.

| 항목 | FLTK X11 | GTK X11 |
| --- | ---: | ---: |
| 동적 링크 실행 파일 | 26,936바이트 | 22,760바이트 |
| `ldd`에 나타난 공유 파일 합계 | 25,080,200바이트 (56개) | 36,784,584바이트 (67개) |

이 합계에는 이미 설치된 공통 라이브러리도 포함되며 플러그인, 글꼴, 데이터 파일과 패키지 의존성이 빠진다. 따라서 다운로드·추가 설치 크기나 실제 메모리 사용량이 아니다. FLTK 공유 라이브러리는 별도 strip 전 파일을 사용해 앞선 strip 측정과도 조건이 다르다. 배포 기준을 정하려면 실제 패키지를 동일한 깨끗한 시스템에 설치해 추가 용량을 측정해야 한다.

IBus 한글 입력을 Xvfb 컨테이너에서 시도했으나 입력기 버스 연결에 실패해 결과를 판정할 수 없었다. 실제 한국어 입력기 환경 검증이 필요하다. FLTK 트레이 오른쪽 메뉴 선택과 시각적 투명도도 미검증이다.

## 추가 검증과 측정 (2026-10-31)

- Xvfb·Openbox·stalonetray에서 FLTK 트레이 우클릭 메뉴의 열기 항목을 키보드로 선택하고 상세 창이 다시 열리는 것을 확인했다. 트레이 호스트 종료 후 재시작해 아이콘이 재표시되는 것도 확인했다. GTK에서는 우클릭 메뉴 표시와 항목 선택을 확인했다.
- Xvfb·Openbox·xcompmgr(`-n`)에서 두 시제품의 미니 창을 화면 캡처하고 불투명도 값을 90%에서 100%로 바꾼 뒤 다시 캡처했다. 창 영역에서 FLTK 7,225픽셀, GTK 8,069픽셀이 달라졌다. `test-opacity.sh`로 재현할 수 있다. 실제 GPU나 다른 합성기에서의 동작은 별도 확인이 필요하다.
- Xvfb·Openbox에서 실행 4초 후 `/proc/<pid>/smaps_rollup`을 한 번 측정했다. FLTK 동적 링크 시제품은 RSS 20,540 KiB, PSS 9,561 KiB였다. GTK 시제품은 기본 렌더러에서 RSS 184,216 KiB, PSS 147,848 KiB였고, `GSK_RENDERER=cairo`로 별도 측정한 RSS는 100,968 KiB, PSS는 65,658 KiB였다. 창 표시·가상 그래픽 환경·공유 메모리와 렌더러에 영향을 받는 단일 측정이며 제품의 유휴 메모리 기준이 아니다.
- 깨끗한 `ubuntu:24.04` 최소 컨테이너에서 `apt-get --assume-no --no-install-recommends` 설치 예측을 비교했다. GTK의 `libgtk-4-1`은 다운로드 37.3 MB, 추가 디스크 160 MB로 예측됐다. FLTK 시제품의 X11·Wayland 관련 공유 라이브러리 패키지 목록은 다운로드 7,925 kB, 추가 디스크 25.5 MB로 예측됐다. FLTK 1.4 라이브러리와 앱 자체는 이 패키지 목록에 포함되지 않으며, 플러그인·글꼴 등 필요한 런타임 자원을 모두 검증한 배포 패키지 비교가 아니다. 기본 데스크톱 시스템의 사전 설치 패키지에 따라 추가 용량은 크게 달라진다.
- IBus Hangul 엔진을 격리 X11에 설치해 두 시제품에서 입력을 시도했다. 버스/포털 연결 메시지가 발생했고 기대한 한글 조합 결과를 확인하지 못했다. 이 시험은 입력기 설정 문제와 앱의 호환성 문제를 구분하지 못하므로 실패 원인을 단정하지 않으며 한글 입력 검증은 미완료다.

위 결과는 X11 시제품의 검증이며, **이 기록 시점에는 Wayland 네이티브 트레이·미니 창이 구현되지 않았다.** 기본 창의 Weston 중첩 실행은 이전 기록과 같지만 실제 GNOME/KDE Wayland 세션 및 한글 입력 검증을 대체하지 못한다. 따라서 전체 플랫폼 최종 검증이나 배포 승인으로 취급하지 않는다.

## Wayland 시제품 (2026-10-31)

`fltk-wayland.cpp`는 FLTK 1.4와 GIO/D-Bus를 이용한 독립적인 실험이다. 기본 창, 미니 창, 폴더 선택과 경로 저장, StatusNotifierItem 및 DBusMenu를 제공한다. 한글 경로는 `$XDG_CONFIG_HOME/ai-mon-wayland-prototype/folder` 또는 `~/.config/ai-mon-wayland-prototype/folder`에 저장한다. FLTK 폴더 선택기는 시스템 환경에 따라 네이티브 대화상자 또는 대체 UI를 사용한다.

현재 코어가 연결된 시제품은 `scripts/linux/build-wayland-prototype.sh`로 빌드한다. FLTK 1.4의 `fltk-config`, GIO 개발 파일과 C++17 컴파일러가 필요하다. 테스트는 저장소 루트에서 실행한다.

```sh
./scripts/linux/build-wayland-prototype.sh
AIMON_PROTOTYPE="$PWD/out/linux-experiment/ai-mon-wayland-prototype" \
AIMON_FAKE_WATCHER="$PWD/experiments/linux-ui-size/fake-watcher.py" \
AIMON_MOCK_CLI="$PWD/tests/linux/mock_cli.py" \
dbus-run-session -- bash experiments/linux-ui-size/test-wayland.sh
```

초기 UI 전용 정적 FLTK 링크 실행 파일은 822,152바이트였으며, 현재 코어 연결 버전의 크기는 별도로 측정해야 한다. GIO·Wayland 등은 여전히 동적 의존성이다. Weston 13을 Xvfb 위에 중첩하고 테스트용 가짜 StatusNotifierWatcher를 사용했다. 한글 경로 직렬화·복원, 기본·미니 Wayland 창 생성, 아이콘 데이터와 메뉴 조회, 메뉴 동작 및 종료, 트레이 서비스 종료 후 재등록을 자동 검증했다. Noto CJK 글꼴이 설치된 환경의 화면 캡처에서 한글 경로와 UI 문구 표시도 확인했다. 이 경로 시험은 폴더 선택 대화상자의 사용자 조작을 자동화한 것은 아니다.

이 시제품은 실제 트레이 UI를 구현하는 호스트가 아니라 가짜 서비스로 D-Bus 교환을 검사한다. 따라서 GNOME/KDE 등 실제 호스트에서 아이콘, 좌·우클릭 및 메뉴 호환성은 아직 미검증이다. `ContextMenu` 직접 호출은 상세 창을 열며, 메뉴는 호스트가 DBusMenu를 사용하는 경우 제공된다. 호스트가 없을 때 상세 창에서 종료할 수 있다.

Wayland 미니 창은 컴포지터에 창 이동을 맡기며 위치 복원과 투명도를 구현하지 않았다. Wayland에서는 일반적으로 클라이언트가 최상위 창의 전역 위치를 제어할 수 없다([FLTK 문서](https://www.fltk.org/doc-1.4/classFl__Window.html)). 트레이 프로토콜은 [StatusNotifierItem 규격](https://specifications.freedesktop.org/status-notifier-item/latest-single/)을 참고했다. 실제 데스크톱별 동작 및 폴더 선택·접근성 검증이 필요하다. 아래 코어 연동 기록을 참고한다.

## 리눅스 코어 연결

기존 사용량 파서와 집계·캐시를 Linux 파일 처리와 연결하고, Claude와 Codex의 공식 CLI를 별도 프로세스로 호출하는 실험 구현을 추가했다. Wayland 시제품은 10초마다 로그를 수집하고 약 2분마다 한도를 조회한다. 한도는 공식 CLI가 응답한 값만 표시하며 알 수 없거나 만료되면 `—`로 표시한다. 프로세스 실행은 셸을 사용하지 않고 시간 제한과 종료 시 취소를 적용한다.

수집기는 `$XDG_STATE_HOME/ai-mon-linux-experiment` 또는 `~/.local/state/ai-mon-linux-experiment`를 사용한다. 기존 Windows 데이터와 별개이며 UI에서 고른 폴더는 Claude 로그 경로다. Codex는 `CODEX_HOME` 또는 기본 홈 경로를 따른다.

`scripts/linux/test-core.sh`는 한글 경로, 부분 JSONL·추가·교체·캐시 복원, UTF-8, 모의 CLI 응답, 오류 보존, 시간 제한과 실행 중 취소를 검증한다. Wayland 테스트는 격리 HOME과 모의 CLI를 사용하여 두 제공자의 로그와 한도 값이 UI 프로세스의 캐시에 반영되는 것을 확인한다. 두 프로그램의 실제 설치·로그인 상태에서의 CLI 호환성 및 실제 트레이 데스크톱은 아직 검증하지 않았다. 이 시제품은 설정 화면, 주간 그래프, X11 UI와 패키징 등 완성 제품의 모든 기능을 포함하지 않는다.

이 PC에 설치된 Codex CLI에서도 별도 임시 캐시를 사용한 실제 계정 한도 조회가 `Ready`로 완료됐다. 계정 값은 이 기록에 포함하지 않는다. Claude CLI는 현재 PATH에서 찾지 못했으므로 실제 로그인 환경에서의 Claude 조회는 아직 미검증이다. Windows 변경 파일은 MinGW 교차 컴파일을 통과했지만 Windows 실행 테스트는 하지 않았다.

### Claude 로그인 없이 실제 CLI 검증

인증 정보가 없는 별도의 Ubuntu 24.04 컨테이너에 공식 네이티브 설치 프로그램으로 Claude Code 안정 버전 2.1.308을 설치해 확인했다. `claude auth status`는 `loggedIn: false`, `authMethod: none`을 보고했다. CLI의 제어 프로토콜에서 초기화와 `get_usage` 요청은 모두 성공했지만 응답은 `rate_limits_available: false`, `rate_limits: null`이었다. 앱의 리눅스 조회기는 CLI를 PATH에서 찾아 `Unavailable`로 분류하고 단기·주간 값이 모두 없는 캐시를 저장했다. 이전 값이 남지 않는 동작을 모의 응답 테스트에도 추가했다.

이 결과는 로그인 없이 설치·프로토콜과 값 없음 처리를 확인할 수 있다는 의미이며, 로그인된 계정의 실제 잔여율과 초기화 시각 조회는 여전히 미검증이다. CLI는 격리 컨테이너에만 설치했으며 호스트의 PATH나 사용자 설정은 변경하지 않았다.
