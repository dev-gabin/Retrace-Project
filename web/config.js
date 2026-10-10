// Jetson의 jetson_app.py가 정적 파일과 API를 같은 주소에서 제공합니다.
export const config = Object.freeze({
  mode: 'live', // 'demo' | 'live'
  apiBaseUrl: '', // 같은 서버면 빈 문자열. 별도 서버 예: http://192.168.0.10:8080
  timeoutMs: 8000,
});
