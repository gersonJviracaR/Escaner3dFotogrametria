// Constantes de la guía de captura (HU-03). Valores por defecto del Sprint 1.

/// Giro objetivo entre tomas consecutivas, en grados (360° / 20 fotos).
const double kTargetStepDegrees = 18;

/// Por debajo de este giro se sigue esperando.
const double kMinStepDegrees = 15;

/// Por encima de este giro se avisa que se giró demasiado.
const double kMaxStepDegrees = 25;

/// Fotos para considerar la sesión completa.
const int kTargetPhotoCount = 20;

/// Mínimo de fotos para permitir el envío.
const int kMinPhotosToSend = 12;
