## AI Mon Windows 0.4.1 베타

Windows 10/11 x64용 포터블 베타입니다. 아래 `ai-mon.exe`를 내려받아 실행하세요. 기존 AI Mon이 실행 중이면 먼저 종료하세요. MSI 설치 파일은 이번 베타에 포함되지 않습니다.

기존 Claude Code·Codex 사용량과 잔여 한도 화면에 **주간·월간 사용 기록**을 추가했습니다. 기간별 합계와 모델·확인 가능한 사용 경로별 입력·출력 토큰을 보고 CSV로 내보낼 수 있습니다. Codex 세션과 보관 세션을 포함하며, 모델이나 CLI·VS Code 출처를 확인할 수 없는 항목은 추측하지 않습니다. 이 PC의 설정된 로컬 기록이 대상이므로 다른 기기·원격 환경의 사용량을 모두 포함하지는 않습니다.

설정과 기존 상태는 유지하며, 새 이력은 `%LOCALAPPDATA%\AI Mon\history.sqlite3`에 저장합니다. 이전에 삭제된 원본이나 기존 상태 캐시에만 남은 데이터는 이력으로 자동 이전하지 않습니다. Windows 시스템의 `winsqlite3.dll`을 이용하며, 사용할 수 없으면 이력 창에 오류를 표시합니다.

실행 파일은 559,616바이트입니다. `SHA256SUMS`로 다운로드 무결성을 확인할 수 있습니다. LLVM-MinGW로 x64·ASLR·DEP·CFG 및 시스템 DLL 의존성을 확인했고, Wine과 SQLite 3.23.1 시험용 DLL에서 기존 코어 및 새 이력·UI 테스트를 통과했습니다.

**아직 실제 Windows 10/11과 Microsoft 기본 SQLite에서 실행 검증하지 않았습니다.** 실제 Windows 트레이, DPI, CSV 저장 대화상자와 Claude VS Code 출처 구분도 추가 확인이 필요합니다. 자세한 내용은 태그에 포함된 `docs/WINDOWS_HISTORY.md`를 참고하세요.
