# Linux 0.4.0~beta1 검증 기록

- 대상: Ubuntu 24.04 x86-64, FLTK 1.4.5, C++17, GIO/D-Bus.
- 패키지: `out/linux-release/ai-mon-linux_0.4.0~beta1_amd64.deb`
- 크기: 365,040바이트. 설치에 필요한 시스템 라이브러리 크기는 별도다.
- SHA-256: `292e664f1dd6865a7f2432fcb5c9e186619a1bd16d3ec6c6484bb80f01283fe4`
- 빌드: Ubuntu 24.04 컨테이너에서 FLTK X11·Wayland 백엔드 활성화 및 GIO 링크. Ubuntu 배포판에서 의존성을 생성하고, FLTK는 실행 파일에 정적 링크했다.

## 검증

- `scripts/linux/test-core.sh`: 로컬 로그의 부분 줄·추가·교체·캐시 복구, 한글 경로, UTF-8 검증, 모의 CLI 정상 응답·오류·제한 시간·취소.
- `tests/linux/test-ui-x11.sh`, `tests/linux/test-ui-wayland.sh`: 가상 X11 및 Weston 중첩 Wayland 세션에서 테스트용 로그와 모의 CLI 수집, 트레이 등록·메뉴·재등록·종료. 두 테스트에서 트레이 픽셀의 모의 한도 반영을 확인했다. X11 테스트에서 중복 실행을 거부하는 것을 확인했다.
- 빈 Ubuntu 24.04 컨테이너에서 패키지와 의존성을 설치하고 `ldd` 누락이 없음을 확인했다. 패키지 제거 후 사용자 설정과 상태 파일이 유지되는 것도 확인했다.
- 실제 Codex CLI의 계정 한도 조회는 이 PC에서 확인했다. 별도 Ubuntu 컨테이너의 Claude Code 2.1.308은 로그인하지 않은 상태에서 정상 조회 응답과 값 없음 처리를 확인했다.
- 변경한 Windows 수집·사용량 코드의 MinGW 교차 컴파일은 통과했다. 기존 Windows 전체 테스트는 실행하지 않았다.

## 남은 검증과 제한

로그인된 Claude 계정의 실제 잔여율·초기화 시각, 실제 GNOME/KDE 트레이 호환성, 모든 배율·접근성 및 장시간 실행은 미검증이다. 트레이 없는 환경에서는 상세 창에서 조작한다. Wayland에서는 미니 창 위치 복원과 투명도 설정이 없다. 상세 설정·언어 전환·Windows 버전과 동등한 그래프는 베타에 포함되지 않는다. 연결되지 않은 한도는 `—`이며 이 값은 0%를 뜻하지 않는다.

UI 자동 검사는 저장소 루트에서 다음처럼 실행한다. 테스트 의존성은 Xvfb, Weston, `xdotool`, `gdbus`, Python GObject와 Openbox 또는 테스트 가짜 트레이 호스트다. 별도 세션과 모의 CLI를 사용한다.

```bash
export AIMON_PROTOTYPE="$PWD/out/linux-release/ai-mon"
export AIMON_FAKE_WATCHER="$PWD/experiments/linux-ui-size/fake-watcher.py"
export AIMON_MOCK_CLI="$PWD/tests/linux/mock_cli.py"
export AIMON_ICON_CHECK="$PWD/tests/linux/check-tray-icon.py"
dbus-run-session -- bash tests/linux/test-ui-x11.sh
dbus-run-session -- bash tests/linux/test-ui-wayland.sh
```
