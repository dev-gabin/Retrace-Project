const image = document.getElementById('preview-image');
const status = document.getElementById('preview-status');

function refreshPreview() {
  image.src = `/api/camera-preview.jpg?t=${Date.now()}`;
}

image.addEventListener('load', () => {
  status.textContent = '실시간 미리보기 연결됨';
});

image.addEventListener('error', () => {
  status.textContent = '카메라 프레임을 기다리는 중';
});

refreshPreview();
window.setInterval(refreshPreview, 500);
