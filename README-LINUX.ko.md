# AI Mon Linux 베타

Ubuntu 24.04 x86-64용 설치 패키지입니다. Claude Code와 Codex의 로컬 토큰 사용량과 공식 CLI가 보고한 계정 잔여 한도를 표시합니다. 계정이 로그인되지 않았거나 한도 정보를 얻지 못하면 `—`로 표시합니다.

## 설치와 실행

이 저장소에서 빌드된 패키지가 있을 때:

```bash
sudo apt install ./out/linux-release/ai-mon-linux_0.4.1~beta1_amd64.deb
ai-mon
```

설치 후 앱 목록에서도 **AI Mon**을 실행할 수 있습니다. 한국어 글꼴이 없는 환경에서는 배포판의 한국어 글꼴을 설치하세요. 예를 들어 Ubuntu에서는 `sudo apt install fonts-noto-cjk`를 사용할 수 있습니다.

- Claude와 Codex의 단기·주간 잔여율 및 이 PC에서 읽은 오늘의 입력·출력 토큰을 표시합니다. 남은 한도를 로컬 토큰에서 추정하지 않습니다.
- **주간·월간 기록**에서 기간 합계와 제공자·모델·확인 가능한 사용 경로별 토큰을 보고, 현재 보기의 집계를 UTF-8 CSV로 내보낼 수 있습니다. 기록은 이 PC의 원본 로그에서 수집하며 다른 기기·웹·원격 환경의 사용량까지 보장하지 않습니다. 모델이나 경로를 확인할 수 없으면 `알 수 없음`으로 표시합니다.
- Codex 기본 `sessions` 경로를 사용할 때 같은 상위 폴더의 `archived_sessions`도 이력에 포함합니다. 한 번 수집한 이력은 원본 파일이 삭제된 뒤에도 남습니다.
- 설치되고 로그인된 공식 CLI를 약 2분마다 호출해 한도를 조회합니다. 로그인하지 않은 Claude는 한도 없음으로 표시됩니다. `새로고침`은 수집을 요청합니다.
- 각 제공자의 로그 폴더는 버튼으로 바꿀 수 있습니다. 기본 경로는 `$CLAUDE_CONFIG_DIR/projects` 또는 `~/.claude/projects`, `$CODEX_HOME/sessions` 또는 `~/.codex/sessions`입니다.
- 미니 창과 StatusNotifierItem 트레이를 지원합니다. 트레이를 제공하지 않는 데스크톱에서는 상세 창의 종료 버튼을 사용합니다. 트레이 막대는 왼쪽부터 Claude 단기·주간, Codex 단기·주간입니다. 회색 짧은 막대는 확인할 수 없는 값입니다.
- 트레이 서비스나 데스크톱 설정에 따라 아이콘이 표시되지 않을 수 있습니다. Wayland에서 미니 창의 위치 복원과 투명도 설정은 제공하지 않습니다.

설정은 `${XDG_CONFIG_HOME:-~/.config}/ai-mon/`, 캐시, 이력 데이터베이스와 단일 인스턴스 잠금 파일은 `${XDG_STATE_HOME:-~/.local/state}/ai-mon/`에 저장합니다. 로그 원본은 수정하지 않으며, 캐시에는 집계에 필요한 값이 들어갑니다. 대화 본문과 인증 정보를 앱이 별도로 저장하지 않습니다.

제거하려면 `sudo apt remove ai-mon-linux`를 실행합니다. 설정과 캐시는 제거 후에도 남습니다.

## 빌드

C++17 컴파일러, `pkg-config`, GIO와 SQLite 개발 파일, X11과 Wayland를 모두 활성화한 **FLTK 1.4** 개발 환경, Debian 패키징 도구가 필요합니다. 배포 파일은 Ubuntu 24.04 amd64 환경에서 FLTK 1.4.5를 크기 최적화해 정적 링크했습니다. 그 밖의 시스템 라이브러리는 패키지 의존성으로 설치됩니다.

```bash
./scripts/linux/test-core.sh
./scripts/linux/test-history.sh
FLTK_CONFIG=/path/to/fltk-config ./scripts/linux/build.sh
./scripts/linux/package-deb.sh
```

빌드 스크립트는 FLTK를 자동으로 내려받지 않습니다. 기존 공개 베타 검증 기록은 [리눅스 릴리스 기록](docs/LINUX_RELEASE.md), 이력 기능의 의미와 제한은 [이력 기록 안내](docs/LINUX_HISTORY.md)에 있습니다.

## 베타 범위

X11과 Wayland의 자동 UI 검사는 가상 화면과 가짜 트레이 호스트로 수행했습니다. 실제 GNOME/KDE 트레이 및 로그인된 Claude 계정의 한도 응답은 아직 확인하지 않았습니다. Windows 버전의 모든 설정, 그래프와 번역 기능을 포함하는 정식 릴리스는 아닙니다.
