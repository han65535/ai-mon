## AI Mon Linux 0.4.0 베타 1

Ubuntu 24.04 x86-64용 Linux 베타입니다. 아래 `.deb`를 내려받아 설치할 수 있습니다.

```bash
sudo apt install ./ai-mon-linux_0.4.0~beta1_amd64.deb
ai-mon
```

Claude Code와 Codex의 로컬 토큰 사용량과 공식 CLI가 보고한 단기·주간 잔여 한도를 표시합니다. 로그인하지 않았거나 한도 정보를 확인할 수 없으면 `—`로 표시합니다. X11과 Wayland 창, 미니 창, StatusNotifierItem 트레이, 제공자별 로그 폴더 선택을 지원합니다. 트레이가 없는 환경에서는 상세 창에서 종료할 수 있습니다.

패키지 크기는 365,040바이트입니다. 시스템 라이브러리는 별도로 필요합니다. SHA-256은 함께 첨부한 `SHA256SUMS`에서 확인할 수 있습니다.

로그인된 Claude 계정의 실제 잔여율·초기화 시각과 실제 GNOME/KDE 트레이 호환성은 아직 검증하지 않았습니다. Wayland 미니 창의 위치 복원과 투명도 설정, Windows 버전과 동등한 모든 설정·그래프는 이번 베타에 포함되지 않습니다. 한국어 글꼴이 없다면 Ubuntu에서 `sudo apt install fonts-noto-cjk`로 설치할 수 있습니다.

설정과 캐시는 패키지를 제거해도 유지됩니다. 자세한 사용법과 검증 범위는 저장소의 `README-LINUX.ko.md`와 `docs/LINUX_RELEASE.md`를 참고하세요.
