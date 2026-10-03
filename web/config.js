// 실제 서버 연결 전까지 demo를 유지합니다. API 임시안: docs/protocol.md 7-1.
export const config = Object.freeze({
  mode: 'demo', // 'demo' | 'live'
  apiBaseUrl: '', // 같은 서버면 빈 문자열. 별도 서버 예: http://192.168.0.10:8080
  timeoutMs: 8000,
});
