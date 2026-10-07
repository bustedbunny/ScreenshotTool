#include "localization.hpp"
#include <algorithm>
#include <iterator>
#include <limits>

namespace shot {
namespace {
struct CatalogEntry {
    TextId id;
    std::array<std::wstring_view,6> values;
};
// Columns are English, Russian, simplified Chinese, Japanese, German, Spanish.
// Every sentence owns its placeholders, so translations may reorder arguments.
constexpr CatalogEntry catalog[]{
    {TextId::Automatic,{L"Automatic (system language)",L"Автоматически (язык системы)",L"自动（系统语言）",L"自動（システムの言語）",L"Automatisch (Systemsprache)",L"Automático (idioma del sistema)"}},
    {TextId::SettingsTitle,{L"ScreenshotTool settings",L"Настройки ScreenshotTool",L"ScreenshotTool 设置",L"ScreenshotTool の設定",L"ScreenshotTool-Einstellungen",L"Configuración de ScreenshotTool"}},
    {TextId::SettingsHeader,{L"ScreenshotTool {0} — Capture with Print Screen",L"ScreenshotTool {0} — Снимок клавишей Print Screen",L"ScreenshotTool {0} — 按 Print Screen 截图",L"ScreenshotTool {0} — Print Screen で撮影",L"ScreenshotTool {0} — Aufnehmen mit Print Screen",L"ScreenshotTool {0} — Capturar con Print Screen"}},
    {TextId::SettingsInstructions,{L"Drag to select, then annotate or export.\nQuick saves: Pictures \\ ScreenshotTool\nHDR selections save an SDR PNG and a lossless HDR JPEG XR.",L"Выделите область, затем добавьте пометки или сохраните.\nБыстрое сохранение: Изображения \\ ScreenshotTool\nОбласти HDR сохраняются как SDR PNG и HDR JPEG XR без потерь.",L"拖动以选择区域，然后添加标注或导出。\n快速保存：图片 \\ ScreenshotTool\nHDR 区域将保存为 SDR PNG 和无损 HDR JPEG XR。",L"ドラッグで範囲を選び、注釈を追加するか書き出します。\nクイック保存先：ピクチャ \\ ScreenshotTool\nHDR 範囲は SDR PNG と可逆圧縮の HDR JPEG XR で保存されます。",L"Bereich ziehen, dann markieren oder exportieren.\nSchnellspeicherung: Bilder \\ ScreenshotTool\nHDR-Bereiche werden als SDR-PNG und verlustfreies HDR-JPEG-XR gespeichert.",L"Arrastra para seleccionar, luego anota o exporta.\nGuardado rápido: Imágenes \\ ScreenshotTool\nLas selecciones HDR se guardan como PNG SDR y JPEG XR HDR sin pérdida."}},
    {TextId::SettingsPrintScreen,{L"If Windows also opens Snipping Tool, turn off 'Use the Print Screen key to open screen capture' in Windows Settings > Accessibility > Keyboard.",L"Если Windows также открывает «Ножницы», отключите «Использовать клавишу Print Screen для открытия захвата экрана» в Параметры Windows > Специальные возможности > Клавиатура.",L"如果 Windows 同时打开截图工具，请在 Windows 设置 > 辅助功能 > 键盘中关闭“使用 Print Screen 键打开屏幕截图”。",L"Windows の Snipping Tool も開く場合は、Windows の設定 > アクセシビリティ > キーボードで「Print Screen キーを使用して画面キャプチャを開く」をオフにしてください。",L"Wenn Windows auch das Snipping Tool öffnet, deaktivieren Sie in Windows-Einstellungen > Barrierefreiheit > Tastatur die Option zum Öffnen der Bildschirmaufnahme mit Print Screen.",L"Si Windows también abre Recortes, desactiva «Usar la tecla Impr Pant para abrir la captura de pantalla» en Configuración de Windows > Accesibilidad > Teclado."}},
    {TextId::AutomaticUpdates,{L"Automatically check for updates at startup",L"Проверять обновления при запуске",L"启动时自动检查更新",L"起動時に更新を自動確認",L"Beim Start automatisch nach Updates suchen",L"Buscar actualizaciones al iniciar"}},
    {TextId::LaunchAtSignIn,{L"Launch ScreenshotTool when I sign in",L"Запускать ScreenshotTool при входе в систему",L"登录时启动 ScreenshotTool",L"サインイン時に ScreenshotTool を起動",L"ScreenshotTool bei der Anmeldung starten",L"Iniciar ScreenshotTool al iniciar sesión"}},
    {TextId::SignInHelp,{L"Sign-in uses this executable's location. If you move it, disable and enable sign-in again.",L"Автозапуск использует расположение этого файла. Если переместите его, выключите и снова включите автозапуск.",L"登录启动使用此可执行文件的位置。如果移动了文件，请关闭并重新启用登录启动。",L"サインイン時の起動にはこの実行ファイルの場所が使われます。移動した場合は、この設定を一度オフにして再度オンにしてください。",L"Der Autostart verwendet den Speicherort dieser Datei. Deaktivieren und aktivieren Sie ihn nach dem Verschieben erneut.",L"El inicio de sesión usa la ubicación de este ejecutable. Si lo mueves, desactiva y vuelve a activar esta opción."}},
    {TextId::LanguageLabel,{L"Language",L"Язык",L"语言",L"言語",L"Sprache",L"Idioma"}},
    {TextId::Ok,{L"OK",L"ОК",L"确定",L"OK",L"OK",L"Aceptar"}},
    {TextId::Cancel,{L"Cancel",L"Отмена",L"取消",L"キャンセル",L"Abbrechen",L"Cancelar"}},
    {TextId::Apply,{L"Apply",L"Применить",L"应用",L"適用",L"Übernehmen",L"Aplicar"}},
    {TextId::Select,{L"Select",L"Выбор",L"选择",L"選択",L"Auswahl",L"Seleccionar"}},
    {TextId::Pen,{L"Pen",L"Перо",L"画笔",L"ペン",L"Stift",L"Lápiz"}},
    {TextId::Highlight,{L"Highlight",L"Маркер",L"荧光笔",L"蛍光ペン",L"Marker",L"Resaltar"}},
    {TextId::Rectangle,{L"Rectangle",L"Прямоугольник",L"矩形",L"四角形",L"Rechteck",L"Rectángulo"}},
    {TextId::Ellipse,{L"Ellipse",L"Эллипс",L"椭圆",L"楕円",L"Ellipse",L"Elipse"}},
    {TextId::Line,{L"Line",L"Линия",L"直线",L"直線",L"Linie",L"Línea"}},
    {TextId::Arrow,{L"Arrow",L"Стрелка",L"箭头",L"矢印",L"Pfeil",L"Flecha"}},
    {TextId::Text,{L"Text",L"Текст",L"文本",L"テキスト",L"Text",L"Texto"}},
    {TextId::Censor,{L"Censor",L"Скрыть",L"遮挡",L"隠す",L"Verdecken",L"Ocultar"}},
    {TextId::SelectTip,{L"Select: drag outside the crop with any tool to replace it. Crop resize handles work with every tool.",L"Выбор: начните выделение вне области любым инструментом, чтобы заменить её. Изменять размер области можно с любым инструментом.",L"选择：使用任意工具在裁剪区域外拖动可重新选择区域。所有工具均可使用裁剪缩放手柄。",L"選択：どのツールでも切り抜き範囲の外からドラッグすると範囲を選び直せます。範囲のサイズ変更ハンドルはすべてのツールで使えます。",L"Auswahl: Ziehen Sie mit einem beliebigen Werkzeug außerhalb des Ausschnitts, um ihn zu ersetzen. Die Größenanfasser funktionieren mit jedem Werkzeug.",L"Seleccionar: arrastra fuera del recorte con cualquier herramienta para reemplazarlo. Los tiradores de tamaño funcionan con todas las herramientas."}},
    {TextId::PenTip,{L"Pen: start a freehand stroke inside the crop; continue anywhere on the captured desktop.",L"Перо: начните свободную линию внутри области; продолжайте в любом месте снимка рабочего стола.",L"画笔：在裁剪区域内开始自由绘制，可继续绘制到已捕获桌面的任意位置。",L"ペン：範囲内から自由に描き始め、撮影したデスクトップのどこへでも続けられます。",L"Stift: Beginnen Sie einen Freihandstrich im Ausschnitt und zeichnen Sie auf dem gesamten aufgenommenen Desktop weiter.",L"Lápiz: inicia un trazo libre dentro del recorte y continúa en cualquier parte del escritorio capturado."}},
    {TextId::HighlightTip,{L"Highlighter: start a translucent wide stroke inside the crop; continue anywhere on the captured desktop.",L"Маркер: начните широкую полупрозрачную линию внутри области; продолжайте в любом месте снимка рабочего стола.",L"荧光笔：在裁剪区域内开始绘制半透明宽笔画，可继续到已捕获桌面的任意位置。",L"蛍光ペン：範囲内から半透明の太い線を描き始め、撮影したデスクトップのどこへでも続けられます。",L"Marker: Beginnen Sie einen breiten, halbtransparenten Strich im Ausschnitt und zeichnen Sie auf dem gesamten aufgenommenen Desktop weiter.",L"Resaltar: inicia un trazo ancho y translúcido dentro del recorte y continúa en cualquier parte del escritorio capturado."}},
    {TextId::RectangleTip,{L"Rectangle: start an outline inside the crop; drag anywhere on the captured desktop.",L"Прямоугольник: начните контур внутри области; ведите в любое место снимка рабочего стола.",L"矩形：在裁剪区域内开始绘制轮廓，可拖动到已捕获桌面的任意位置。",L"四角形：範囲内から輪郭を描き始め、撮影したデスクトップのどこへでもドラッグできます。",L"Rechteck: Beginnen Sie den Umriss im Ausschnitt und ziehen Sie an eine beliebige Stelle des aufgenommenen Desktops.",L"Rectángulo: inicia el contorno dentro del recorte y arrastra a cualquier parte del escritorio capturado."}},
    {TextId::EllipseTip,{L"Ellipse: start an oval outline inside the crop; drag anywhere on the captured desktop.",L"Эллипс: начните овальный контур внутри области; ведите в любое место снимка рабочего стола.",L"椭圆：在裁剪区域内开始绘制椭圆轮廓，可拖动到已捕获桌面的任意位置。",L"楕円：範囲内から楕円の輪郭を描き始め、撮影したデスクトップのどこへでもドラッグできます。",L"Ellipse: Beginnen Sie den ovalen Umriss im Ausschnitt und ziehen Sie an eine beliebige Stelle des aufgenommenen Desktops.",L"Elipse: inicia el contorno ovalado dentro del recorte y arrastra a cualquier parte del escritorio capturado."}},
    {TextId::LineTip,{L"Line: start inside the crop; drag to any desktop point.",L"Линия: начните внутри области; ведите к любой точке рабочего стола.",L"直线：从裁剪区域内开始，拖动到桌面的任意位置。",L"直線：範囲内から始め、デスクトップの任意の位置までドラッグします。",L"Linie: Beginnen Sie im Ausschnitt und ziehen Sie zu einem beliebigen Punkt auf dem Desktop.",L"Línea: comienza dentro del recorte y arrastra a cualquier punto del escritorio."}},
    {TextId::ArrowTip,{L"Arrow: start inside the crop; point at any desktop detail.",L"Стрелка: начните внутри области; укажите любую деталь рабочего стола.",L"箭头：从裁剪区域内开始，指向桌面的任意细节。",L"矢印：範囲内から始め、デスクトップの任意の部分を指し示します。",L"Pfeil: Beginnen Sie im Ausschnitt und zeigen Sie auf ein beliebiges Detail des Desktops.",L"Flecha: comienza dentro del recorte y apunta a cualquier detalle del escritorio."}},
    {TextId::TextTip,{L"Text: click or drag inside the crop, type, then Ctrl+Enter or click outside the box to finish. Active boxes can be edited outside the crop.",L"Текст: нажмите или протяните внутри области, введите текст, затем нажмите Ctrl+Enter или вне рамки для завершения. Активную рамку можно редактировать вне области.",L"文本：在裁剪区域内单击或拖动，输入文字，然后按 Ctrl+Enter 或单击文本框外以完成。活动文本框可在裁剪区域外编辑。",L"テキスト：範囲内でクリックまたはドラッグして入力し、Ctrl+Enter または枠の外のクリックで完了します。編集中の枠は範囲外でも編集できます。",L"Text: Klicken oder ziehen Sie im Ausschnitt, geben Sie Text ein und schließen Sie mit Ctrl+Enter oder einem Klick außerhalb des Feldes ab. Aktive Felder lassen sich außerhalb des Ausschnitts bearbeiten.",L"Texto: haz clic o arrastra dentro del recorte, escribe y termina con Ctrl+Enter o haciendo clic fuera del cuadro. Los cuadros activos se pueden editar fuera del recorte."}},
    {TextId::CensorTip,{L"Censor: start an opaque black cover inside the crop; drag anywhere (or choose pixelation).",L"Скрыть: начните непрозрачное чёрное покрытие внутри области; ведите в любое место (или выберите пикселизацию).",L"遮挡：在裁剪区域内开始绘制不透明黑色遮挡，可拖动到任意位置（也可选择像素化）。",L"隠す：範囲内から黒い不透明の覆いを描き、任意の位置までドラッグします。モザイクも選べます。",L"Verdecken: Beginnen Sie eine deckende schwarze Fläche im Ausschnitt und ziehen Sie an eine beliebige Stelle; alternativ können Sie verpixeln.",L"Ocultar: inicia una cubierta negra opaca dentro del recorte y arrastra a cualquier lugar, o elige pixelar."}},
    {TextId::Color,{L"Color",L"Цвет",L"颜色",L"色",L"Farbe",L"Color"}},
    {TextId::Width,{L"Width",L"Толщина",L"线宽",L"線の太さ",L"Breite",L"Grosor"}},
    {TextId::TextSize,{L"Text size",L"Размер текста",L"字号",L"文字サイズ",L"Textgröße",L"Tamaño del texto"}},
    {TextId::CoverBlack,{L"Cover: black",L"Скрытие: чёрное",L"遮挡：黑色",L"隠す：黒",L"Abdeckung: schwarz",L"Cubierta: negra"}},
    {TextId::CoverPixelate,{L"Cover: pixelate",L"Скрытие: пиксели",L"遮挡：像素化",L"隠す：モザイク",L"Abdeckung: verpixelt",L"Cubierta: pixelada"}},
    {TextId::ColorTip,{L"Choose the annotation color.",L"Выберите цвет пометок.",L"选择标注颜色。",L"注釈の色を選びます。",L"Wählen Sie die Farbe der Markierungen.",L"Elige el color de las anotaciones."}},
    {TextId::WidthTip,{L"Stroke width in captured desktop pixels.",L"Толщина линии в пикселях снимка рабочего стола.",L"笔画宽度，以捕获桌面的像素为单位。",L"線の太さ。撮影したデスクトップのピクセル単位です。",L"Strichbreite in Pixeln des aufgenommenen Desktops.",L"Grosor del trazo en píxeles del escritorio capturado."}},
    {TextId::TextSizeTip,{L"Text size in captured desktop pixels.",L"Размер текста в пикселях снимка рабочего стола.",L"文字大小，以捕获桌面的像素为单位。",L"文字サイズ。撮影したデスクトップのピクセル単位です。",L"Textgröße in Pixeln des aufgenommenen Desktops.",L"Tamaño del texto en píxeles del escritorio capturado."}},
    {TextId::CensorModeTip,{L"Opaque black is the default censor. Pixelation is optional.",L"По умолчанию используется непрозрачное чёрное покрытие. Можно выбрать пикселизацию.",L"默认使用不透明黑色遮挡，也可选择像素化。",L"標準の隠し方は黒い不透明の覆いです。モザイクも選べます。",L"Die Standardabdeckung ist deckend schwarz. Verpixelung ist optional.",L"La cubierta predeterminada es negra y opaca. También puedes pixelar."}},
    {TextId::Undo,{L"Undo",L"Отменить",L"撤销",L"元に戻す",L"Rückgängig",L"Deshacer"}},
    {TextId::Redo,{L"Redo",L"Повторить",L"重做",L"やり直す",L"Wiederholen",L"Rehacer"}},
    {TextId::Copy,{L"Copy",L"Копировать",L"复制",L"コピー",L"Kopieren",L"Copiar"}},
    {TextId::Save,{L"Save",L"Сохранить",L"保存",L"保存",L"Speichern",L"Guardar"}},
    {TextId::SaveAs,{L"Save as",L"Сохранить как",L"另存为",L"名前を付けて保存",L"Speichern unter",L"Guardar como"}},
    {TextId::UndoTip,{L"Undo annotation (Ctrl+Z)",L"Отменить пометку (Ctrl+Z)",L"撤销标注 (Ctrl+Z)",L"注釈を元に戻す (Ctrl+Z)",L"Markierung rückgängig machen (Ctrl+Z)",L"Deshacer anotación (Ctrl+Z)"}},
    {TextId::RedoTip,{L"Redo annotation (Ctrl+Y)",L"Повторить пометку (Ctrl+Y)",L"重做标注 (Ctrl+Y)",L"注釈をやり直す (Ctrl+Y)",L"Markierung wiederherstellen (Ctrl+Y)",L"Rehacer anotación (Ctrl+Y)"}},
    {TextId::CopyTip,{L"Copy SDR image (Ctrl+C)",L"Копировать SDR-изображение (Ctrl+C)",L"复制 SDR 图像 (Ctrl+C)",L"SDR 画像をコピー (Ctrl+C)",L"SDR-Bild kopieren (Ctrl+C)",L"Copiar imagen SDR (Ctrl+C)"}},
    {TextId::SaveTip,{L"Quick save to Pictures / ScreenshotTool (Ctrl+S)",L"Быстро сохранить в Изображения / ScreenshotTool (Ctrl+S)",L"快速保存到图片 / ScreenshotTool (Ctrl+S)",L"ピクチャ / ScreenshotTool にクイック保存 (Ctrl+S)",L"In Bilder / ScreenshotTool speichern (Ctrl+S)",L"Guardar en Imágenes / ScreenshotTool (Ctrl+S)"}},
    {TextId::SaveAsTip,{L"Save an SDR PNG to a chosen location (Ctrl+Shift+S)",L"Сохранить SDR PNG в выбранную папку (Ctrl+Shift+S)",L"将 SDR PNG 保存到所选位置 (Ctrl+Shift+S)",L"選んだ場所に SDR PNG を保存 (Ctrl+Shift+S)",L"SDR-PNG an einem gewählten Ort speichern (Ctrl+Shift+S)",L"Guardar un PNG SDR en la ubicación elegida (Ctrl+Shift+S)"}},
    {TextId::CancelTip,{L"Cancel capture (Esc)",L"Отменить снимок (Esc)",L"取消截图 (Esc)",L"撮影をキャンセル (Esc)",L"Aufnahme abbrechen (Esc)",L"Cancelar captura (Esc)"}},
    {TextId::WidthValue,{L"Width: {0} px",L"Толщина: {0} px",L"线宽：{0} px",L"線の太さ：{0} px",L"Breite: {0} px",L"Grosor: {0} px"}},
    {TextId::TextSizeValue,{L"Text: {0} px",L"Текст: {0} px",L"文字：{0} px",L"文字：{0} px",L"Text: {0} px",L"Texto: {0} px"}},
    {TextId::Pixels,{L"{0} px",L"{0} px",L"{0} px",L"{0} px",L"{0} px",L"{0} px"}},
    {TextId::Exporting,{L"Exporting…",L"Экспорт…",L"正在导出…",L"書き出し中…",L"Export läuft…",L"Exportando…"}},
    {TextId::SelectionStatus,{L"{0} × {1} px  ·  {2}  ·  Esc to cancel",L"{0} × {1} px  ·  {2}  ·  Esc — отмена",L"{0} × {1} px  ·  {2}  ·  Esc 取消",L"{0} × {1} px  ·  {2}  ·  Esc でキャンセル",L"{0} × {1} px  ·  {2}  ·  Esc zum Abbrechen",L"{0} × {1} px  ·  {2}  ·  Esc para cancelar"}},
    {TextId::OverlayTitle,{L"ScreenshotTool selection",L"Выделение ScreenshotTool",L"ScreenshotTool 选区",L"ScreenshotTool の選択範囲",L"ScreenshotTool-Auswahl",L"Selección de ScreenshotTool"}},
    {TextId::ToolbarTitle,{L"ScreenshotTool tools",L"Инструменты ScreenshotTool",L"ScreenshotTool 工具",L"ScreenshotTool のツール",L"ScreenshotTool-Werkzeuge",L"Herramientas de ScreenshotTool"}},
    {TextId::TrayTip,{L"ScreenshotTool — Print Screen to capture",L"ScreenshotTool — Print Screen для снимка",L"ScreenshotTool — 按 Print Screen 截图",L"ScreenshotTool — Print Screen で撮影",L"ScreenshotTool — Aufnehmen mit Print Screen",L"ScreenshotTool — Capturar con Print Screen"}},
    {TextId::Ready,{L"Ready. Press Print Screen to freeze your displays and select a region.",L"Готово. Нажмите Print Screen, чтобы зафиксировать экраны и выбрать область.",L"已就绪。按 Print Screen 冻结屏幕并选择区域。",L"準備完了。Print Screen を押して画面を固定し、範囲を選んでください。",L"Bereit. Drücken Sie Print Screen, um die Bildschirme einzufrieren und einen Bereich auszuwählen.",L"Listo. Pulsa Print Screen para congelar las pantallas y seleccionar una región."}},
    {TextId::Capture,{L"Capture",L"Снимок",L"截图",L"撮影",L"Aufnehmen",L"Capturar"}},
    {TextId::CaptureMenu,{L"Capture\tPrint Screen",L"Снимок\tPrint Screen",L"截图\tPrint Screen",L"撮影\tPrint Screen",L"Aufnehmen\tPrint Screen",L"Capturar\tPrint Screen"}},
    {TextId::OpenFolder,{L"Open screenshots folder",L"Открыть папку снимков",L"打开截图文件夹",L"スクリーンショットのフォルダーを開く",L"Screenshot-Ordner öffnen",L"Abrir carpeta de capturas"}},
    {TextId::Settings,{L"Settings",L"Настройки",L"设置",L"設定",L"Einstellungen",L"Configuración"}},
    {TextId::Exit,{L"Exit",L"Выход",L"退出",L"終了",L"Beenden",L"Salir"}},
    {TextId::CheckUpdates,{L"Check for updates",L"Проверить обновления",L"检查更新",L"更新を確認",L"Nach Updates suchen",L"Buscar actualizaciones"}},
    {TextId::UpdateBusy,{L"Checking / downloading update…",L"Проверка / загрузка обновления…",L"正在检查 / 下载更新…",L"更新を確認 / ダウンロード中…",L"Update wird geprüft / heruntergeladen…",L"Buscando / descargando actualización…"}},
    {TextId::InstallUpdate,{L"Install update…",L"Установить обновление…",L"安装更新…",L"更新をインストール…",L"Update installieren…",L"Instalar actualización…"}},
    {TextId::ViewReleases,{L"View GitHub releases",L"Открыть выпуски на GitHub",L"查看 GitHub 发行版本",L"GitHub のリリースを表示",L"GitHub-Veröffentlichungen anzeigen",L"Ver versiones en GitHub"}},
    {TextId::DisplayLayoutChanged,{L"The display layout changed. Press Print Screen to capture the new layout.",L"Расположение экранов изменилось. Нажмите Print Screen, чтобы сделать новый снимок.",L"显示器布局已更改。按 Print Screen 捕获新布局。",L"ディスプレイの配置が変わりました。Print Screen を押して新しい配置を撮影してください。",L"Die Bildschirmanordnung hat sich geändert. Drücken Sie Print Screen, um die neue Anordnung aufzunehmen.",L"La disposición de las pantallas ha cambiado. Pulsa Print Screen para capturar la nueva disposición."}},
    {TextId::DisplayScalingChanged,{L"Display scaling changed. Press Print Screen to capture the new layout.",L"Масштаб экрана изменился. Нажмите Print Screen, чтобы сделать новый снимок.",L"显示缩放已更改。按 Print Screen 捕获新布局。",L"ディスプレイの拡大率が変わりました。Print Screen を押して新しい配置を撮影してください。",L"Die Bildschirmskalierung hat sich geändert. Drücken Sie Print Screen, um die neue Anordnung aufzunehmen.",L"La escala de la pantalla ha cambiado. Pulsa Print Screen para capturar la nueva disposición."}},
    {TextId::ExportRetry,{L"Your selection is still available. Retry the export.",L"Выделенная область сохранена. Повторите экспорт.",L"您的选区仍然可用。请重试导出。",L"選択範囲は保持されています。もう一度書き出してください。",L"Ihre Auswahl ist weiterhin verfügbar. Versuchen Sie den Export erneut.",L"La selección sigue disponible. Vuelve a intentar la exportación."}},
    {TextId::Copied,{L"Screenshot copied.",L"Снимок скопирован.",L"截图已复制。",L"スクリーンショットをコピーしました。",L"Screenshot kopiert.",L"Captura copiada."}},
    {TextId::Saved,{L"Images saved: {0}\nFolder: {1}",L"Сохранено изображений: {0}\nПапка: {1}",L"已保存图像：{0}\n文件夹：{1}",L"保存した画像：{0}\nフォルダー：{1}",L"Gespeicherte Bilder: {0}\nOrdner: {1}",L"Imágenes guardadas: {0}\nCarpeta: {1}"}},
    {TextId::RateLimited,{L"GitHub has limited update requests. Try again later.",L"GitHub ограничил запросы обновлений. Повторите попытку позже.",L"GitHub 已限制更新请求。请稍后重试。",L"GitHub が更新リクエストを制限しています。しばらくしてから再試行してください。",L"GitHub hat Update-Anfragen begrenzt. Versuchen Sie es später erneut.",L"GitHub ha limitado las solicitudes de actualización. Inténtalo de nuevo más tarde."}},
    {TextId::UpdateAvailable,{L"ScreenshotTool {0} is available. Click to install and restart.",L"Доступен ScreenshotTool {0}. Нажмите для установки и перезапуска.",L"ScreenshotTool {0} 已发布。单击以安装并重启。",L"ScreenshotTool {0} が利用できます。クリックしてインストールし、再起動してください。",L"ScreenshotTool {0} ist verfügbar. Klicken Sie zum Installieren und Neustarten.",L"ScreenshotTool {0} está disponible. Haz clic para instalar y reiniciar."}},
    {TextId::UpdateManualAvailable,{L"ScreenshotTool {0} is available for manual installation. Click to open its GitHub release.",L"Доступен ScreenshotTool {0} для ручной установки. Нажмите, чтобы открыть выпуск на GitHub.",L"ScreenshotTool {0} 可手动安装。单击以打开其 GitHub 发行页面。",L"ScreenshotTool {0} を手動でインストールできます。クリックして GitHub のリリースを開いてください。",L"ScreenshotTool {0} ist zur manuellen Installation verfügbar. Klicken Sie, um die GitHub-Veröffentlichung zu öffnen.",L"ScreenshotTool {0} está disponible para instalación manual. Haz clic para abrir su versión en GitHub."}},
    {TextId::UpdateTitle,{L"ScreenshotTool update",L"Обновление ScreenshotTool",L"ScreenshotTool 更新",L"ScreenshotTool の更新",L"ScreenshotTool-Update",L"Actualización de ScreenshotTool"}},
    {TextId::InstallQuestion,{L"Install ScreenshotTool {0}?",L"Установить ScreenshotTool {0}?",L"安装 ScreenshotTool {0}？",L"ScreenshotTool {0} をインストールしますか？",L"ScreenshotTool {0} installieren?",L"¿Instalar ScreenshotTool {0}?"}},
    {TextId::DownloadRestart,{L"Download and restart",L"Загрузить и перезапустить",L"下载并重启",L"ダウンロードして再起動",L"Herunterladen und neu starten",L"Descargar y reiniciar"}},
    {TextId::ViewNotes,{L"View release notes",L"Открыть описание выпуска",L"查看发行说明",L"リリースノートを表示",L"Versionshinweise anzeigen",L"Ver notas de la versión"}},
    {TextId::UpdateExplanation,{L"The verified update will replace this executable and restart ScreenshotTool. Your settings will be preserved.\n\nIf you start a screenshot while downloading, installation waits until you finish.",L"Проверенное обновление заменит этот файл и перезапустит ScreenshotTool. Настройки сохранятся.\n\nЕсли вы начнёте делать снимок во время загрузки, установка дождётся завершения.",L"经过验证的更新将替换此可执行文件并重启 ScreenshotTool。您的设置将保留。\n\n如果在下载时开始截图，安装将等待截图完成。",L"検証済みの更新でこの実行ファイルを置き換え、ScreenshotTool を再起動します。設定は保持されます。\n\nダウンロード中に撮影を始めた場合、撮影が終わるまでインストールを待機します。",L"Das geprüfte Update ersetzt diese ausführbare Datei und startet ScreenshotTool neu. Ihre Einstellungen bleiben erhalten.\n\nWenn Sie während des Downloads eine Aufnahme starten, wartet die Installation, bis Sie fertig sind.",L"La actualización verificada reemplazará este ejecutable y reiniciará ScreenshotTool. Se conservará tu configuración.\n\nSi inicias una captura durante la descarga, la instalación esperará a que termines."}},
    {TextId::Downloading,{L"Downloading and verifying the update. Screenshot capture remains available.",L"Загрузка и проверка обновления. Можно продолжать делать снимки.",L"正在下载并验证更新。您仍可截图。",L"更新をダウンロードして検証しています。撮影は引き続き利用できます。",L"Das Update wird heruntergeladen und geprüft. Bildschirmaufnahmen sind weiterhin möglich.",L"Descargando y verificando la actualización. Puedes seguir haciendo capturas."}},
    {TextId::ManualInstallHelp,{L"Open the GitHub release page to install manually.",L"Для ручной установки откройте страницу выпуска GitHub.",L"打开 GitHub 发行页面以手动安装。",L"手動でインストールするには、GitHub のリリースページを開いてください。",L"Öffnen Sie zur manuellen Installation die GitHub-Veröffentlichungsseite.",L"Abre la página de la versión en GitHub para instalar manualmente."}},
    {TextId::UpToDate,{L"ScreenshotTool is up to date.",L"Установлена последняя версия ScreenshotTool.",L"ScreenshotTool 已是最新版本。",L"ScreenshotTool は最新です。",L"ScreenshotTool ist auf dem neuesten Stand.",L"ScreenshotTool está actualizado."}},
    {TextId::NoNewRelease,{L"No newer stable release is available.",L"Новой стабильной версии нет.",L"没有更新的稳定版本。",L"新しい安定版はありません。",L"Es ist keine neuere stabile Version verfügbar.",L"No hay una versión estable más reciente."}},
    {TextId::NoRelease,{L"No published release is available yet.",L"Опубликованных выпусков пока нет.",L"尚无已发布的版本。",L"公開済みのリリースはまだありません。",L"Es ist noch keine veröffentlichte Version verfügbar.",L"Todavía no hay ninguna versión publicada."}},
    {TextId::IncompatibleRelease,{L"A newer release is available, but it has no compatible update asset. Open its release page to install manually.",L"Доступна новая версия, но в ней нет совместимого файла обновления. Откройте страницу выпуска для ручной установки.",L"有新版本可用，但没有兼容的更新文件。请打开其发行页面以手动安装。",L"新しいリリースがありますが、互換性のある更新ファイルがありません。リリースページを開いて手動でインストールしてください。",L"Eine neuere Version ist verfügbar, enthält aber keine kompatible Update-Datei. Öffnen Sie die Veröffentlichungsseite zur manuellen Installation.",L"Hay una versión más reciente, pero no contiene un archivo de actualización compatible. Abre su página para instalarla manualmente."}},
    {TextId::ReleaseAvailable,{L"ScreenshotTool {0} is available.",L"Доступен ScreenshotTool {0}.",L"ScreenshotTool {0} 已发布。",L"ScreenshotTool {0} が利用できます。",L"ScreenshotTool {0} ist verfügbar.",L"ScreenshotTool {0} está disponible."}},
    {TextId::HttpFailure,{L"GitHub update request failed (HTTP {0}).",L"Ошибка запроса обновления GitHub (HTTP {0}).",L"GitHub 更新请求失败（HTTP {0}）。",L"GitHub の更新リクエストに失敗しました（HTTP {0}）。",L"GitHub-Update-Anfrage fehlgeschlagen (HTTP {0}).",L"La solicitud de actualización a GitHub falló (HTTP {0})."}},
    {TextId::UpdateCanceled,{L"Update check canceled.",L"Проверка обновлений отменена.",L"更新检查已取消。",L"更新の確認をキャンセルしました。",L"Update-Prüfung abgebrochen.",L"Búsqueda de actualizaciones cancelada."}},
    {TextId::InvalidMetadata,{L"GitHub returned invalid release metadata.",L"GitHub вернул некорректные данные выпуска.",L"GitHub 返回了无效的发行元数据。",L"GitHub から無効なリリース情報が返されました。",L"GitHub hat ungültige Versionsmetadaten zurückgegeben.",L"GitHub devolvió metadatos de versión no válidos."}},
    {TextId::MalformedMetadata,{L"GitHub returned malformed release metadata.",L"GitHub вернул данные выпуска в неверном формате.",L"GitHub 返回的发行元数据格式错误。",L"GitHub から形式の壊れたリリース情報が返されました。",L"GitHub hat fehlerhaft formatierte Versionsmetadaten zurückgegeben.",L"GitHub devolvió metadatos de versión mal formados."}},
    {TextId::ParserFailed,{L"Could not initialize the Windows release parser.",L"Не удалось запустить анализатор данных выпуска Windows.",L"无法初始化 Windows 发行信息解析器。",L"Windows のリリース情報解析機能を初期化できませんでした。",L"Der Windows-Parser für Versionsdaten konnte nicht initialisiert werden.",L"No se pudo inicializar el analizador de versiones de Windows."}},
    {TextId::PngFilter,{L"PNG image (*.png)",L"Изображение PNG (*.png)",L"PNG 图像 (*.png)",L"PNG 画像 (*.png)",L"PNG-Bild (*.png)",L"Imagen PNG (*.png)"}},
    {TextId::OpenFolderFailed,{L"Could not open the screenshots folder.",L"Не удалось открыть папку снимков.",L"无法打开截图文件夹。",L"スクリーンショットのフォルダーを開けませんでした。",L"Der Screenshot-Ordner konnte nicht geöffnet werden.",L"No se pudo abrir la carpeta de capturas."}},
    {TextId::OpenReleaseFailed,{L"Could not open the GitHub release page.",L"Не удалось открыть страницу выпуска GitHub.",L"无法打开 GitHub 发行页面。",L"GitHub のリリースページを開けませんでした。",L"Die GitHub-Veröffentlichungsseite konnte nicht geöffnet werden.",L"No se pudo abrir la página de la versión en GitHub."}},
    {TextId::StartupFailed,{L"Could not start ScreenshotTool.",L"Не удалось запустить ScreenshotTool.",L"无法启动 ScreenshotTool。",L"ScreenshotTool を起動できませんでした。",L"ScreenshotTool konnte nicht gestartet werden.",L"No se pudo iniciar ScreenshotTool."}},
    {TextId::SettingsFailed,{L"Could not apply the settings.",L"Не удалось применить настройки.",L"无法应用设置。",L"設定を適用できませんでした。",L"Die Einstellungen konnten nicht angewendet werden.",L"No se pudo aplicar la configuración."}},
    {TextId::CaptureFailed,{L"Could not capture the desktop. Unlock Windows, close other capture software, or update the graphics driver and retry.",L"Не удалось сделать снимок рабочего стола. Разблокируйте Windows, закройте другие программы захвата или обновите графический драйвер и повторите попытку.",L"无法捕获桌面。请解锁 Windows、关闭其他截图软件或更新显卡驱动程序后重试。",L"デスクトップを撮影できませんでした。Windows のロックを解除するか、他の撮影ソフトを閉じるか、グラフィックスドライバーを更新して再試行してください。",L"Der Desktop konnte nicht aufgenommen werden. Entsperren Sie Windows, schließen Sie andere Aufnahmesoftware oder aktualisieren Sie den Grafiktreiber und versuchen Sie es erneut.",L"No se pudo capturar el escritorio. Desbloquea Windows, cierra otros programas de captura o actualiza el controlador gráfico e inténtalo de nuevo."}},
    {TextId::RenderFailed,{L"Could not display the screenshot.",L"Не удалось отобразить снимок.",L"无法显示截图。",L"スクリーンショットを表示できませんでした。",L"Der Screenshot konnte nicht angezeigt werden.",L"No se pudo mostrar la captura."}},
    {TextId::ExportFailed,{L"Could not export the screenshot.",L"Не удалось экспортировать снимок.",L"无法导出截图。",L"スクリーンショットを書き出せませんでした。",L"Der Screenshot konnte nicht exportiert werden.",L"No se pudo exportar la captura."}},
    {TextId::UpdateFailed,{L"Could not update ScreenshotTool.",L"Не удалось обновить ScreenshotTool.",L"无法更新 ScreenshotTool。",L"ScreenshotTool を更新できませんでした。",L"ScreenshotTool konnte nicht aktualisiert werden.",L"No se pudo actualizar ScreenshotTool."}},
    {TextId::SaveSettingsFailed,{L"Could not save the settings.",L"Не удалось сохранить настройки.",L"无法保存设置。",L"設定を保存できませんでした。",L"Die Einstellungen konnten nicht gespeichert werden.",L"No se pudo guardar la configuración."}},
    {TextId::CodecFormat,{L"The Windows codec cannot preserve the requested image format. Repair the Windows image codecs and retry.",L"Кодек Windows не может сохранить нужный формат изображения. Восстановите кодеки изображений Windows и повторите попытку.",L"Windows 编解码器无法保留所需的图像格式。请修复 Windows 图像编解码器后重试。",L"Windows のコーデックでは要求された画像形式を保持できません。Windows の画像コーデックを修復して再試行してください。",L"Der Windows-Codec kann das gewünschte Bildformat nicht beibehalten. Reparieren Sie die Windows-Bildcodecs und versuchen Sie es erneut.",L"El códec de Windows no puede conservar el formato de imagen solicitado. Repara los códecs de imagen de Windows e inténtalo de nuevo."}},
    {TextId::ImageTooLarge,{L"The selected image is too large for the Windows image codec.",L"Выбранное изображение слишком велико для кодека изображений Windows.",L"所选图像超出了 Windows 图像编解码器的大小限制。",L"選択した画像は Windows の画像コーデックで扱うには大きすぎます。",L"Das ausgewählte Bild ist zu groß für den Windows-Bildcodec.",L"La imagen seleccionada es demasiado grande para el códec de imagen de Windows."}},
    {TextId::EncodedTooLarge,{L"Encoded image is too large.",L"Закодированное изображение слишком велико.",L"编码后的图像过大。",L"エンコードした画像が大きすぎます。",L"Das kodierte Bild ist zu groß.",L"La imagen codificada es demasiado grande."}},
    {TextId::IncompleteStream,{L"The encoded image stream is incomplete.",L"Поток закодированного изображения неполон.",L"编码图像流不完整。",L"エンコードした画像データが不完全です。",L"Der kodierte Bilddatenstrom ist unvollständig.",L"El flujo de la imagen codificada está incompleto."}},
    {TextId::ClipboardAllocation,{L"Cannot allocate clipboard image.",L"Не удалось выделить память для изображения в буфере обмена.",L"无法为剪贴板图像分配内存。",L"クリップボードの画像用メモリを確保できません。",L"Speicher für das Zwischenablagebild konnte nicht reserviert werden.",L"No se puede asignar memoria para la imagen del portapapeles."}},
    {TextId::IncompleteWrite,{L"The screenshot file could not be fully written.",L"Не удалось полностью записать файл снимка.",L"无法完整写入截图文件。",L"スクリーンショットのファイルを最後まで書き込めませんでした。",L"Die Screenshot-Datei konnte nicht vollständig geschrieben werden.",L"No se pudo escribir el archivo de captura por completo."}},
    {TextId::UniqueFilename,{L"Could not reserve a unique screenshot filename. Retry saving.",L"Не удалось зарезервировать уникальное имя снимка. Повторите сохранение.",L"无法预留唯一的截图文件名。请重试保存。",L"重複しないスクリーンショットのファイル名を確保できませんでした。もう一度保存してください。",L"Ein eindeutiger Screenshot-Dateiname konnte nicht reserviert werden. Versuchen Sie das Speichern erneut.",L"No se pudo reservar un nombre único para la captura. Vuelve a guardarla."}},
    {TextId::ClipboardTaken,{L"Another application took the clipboard. Retry Copy.",L"Другая программа заняла буфер обмена. Повторите копирование.",L"其他应用程序占用了剪贴板。请重试复制。",L"別のアプリがクリップボードを使用しました。もう一度コピーしてください。",L"Eine andere Anwendung hat die Zwischenablage übernommen. Versuchen Sie das Kopieren erneut.",L"Otra aplicación tomó el portapapeles. Vuelve a copiar."}},
    {TextId::AdapterDisconnected,{L"The display adapter disconnected. Capture again.",L"Графический адаптер отключился. Сделайте снимок заново.",L"显示适配器已断开连接。请重新截图。",L"ディスプレイアダプターが切断されました。もう一度撮影してください。",L"Der Grafikadapter wurde getrennt. Nehmen Sie erneut auf.",L"El adaptador de pantalla se desconectó. Vuelve a capturar."}},
    {TextId::PixelFormat,{L"Unexpected rendering pixel format.",L"Неожиданный формат пикселей при отрисовке.",L"渲染像素格式异常。",L"描画のピクセル形式が想定外です。",L"Unerwartetes Pixelformat beim Rendern.",L"Formato de píxel de renderizado inesperado."}},
    {TextId::DisplayConfigurationChanged,{L"Display configuration changed. Capture again.",L"Конфигурация экранов изменилась. Сделайте снимок заново.",L"显示配置已更改。请重新截图。",L"ディスプレイの設定が変わりました。もう一度撮影してください。",L"Die Bildschirmkonfiguration hat sich geändert. Nehmen Sie erneut auf.",L"La configuración de pantalla ha cambiado. Vuelve a capturar."}},
    {TextId::HdrBrightness,{L"Cannot read the HDR monitor's SDR brightness. Reconnect the display or update its graphics driver, then capture again.",L"Не удалось прочитать яркость SDR на HDR-мониторе. Переподключите экран или обновите графический драйвер, затем сделайте снимок заново.",L"无法读取 HDR 显示器的 SDR 亮度。请重新连接显示器或更新显卡驱动程序，然后重新截图。",L"HDR モニターの SDR の明るさを読み取れません。ディスプレイを接続し直すかグラフィックスドライバーを更新して、もう一度撮影してください。",L"Die SDR-Helligkeit des HDR-Monitors kann nicht gelesen werden. Schließen Sie den Bildschirm erneut an oder aktualisieren Sie den Grafiktreiber und nehmen Sie erneut auf.",L"No se puede leer el brillo SDR del monitor HDR. Vuelve a conectar la pantalla o actualiza su controlador gráfico y captura de nuevo."}},
    {TextId::HdrSurface,{L"The graphics driver returned an SDR surface for an HDR display. Update the driver or disable HDR explicitly before retrying; this capture cannot preserve HDR.",L"Графический драйвер вернул SDR-изображение для HDR-экрана. Обновите драйвер или явно отключите HDR перед повторной попыткой; этот снимок не может сохранить HDR.",L"显卡驱动程序为 HDR 显示器返回了 SDR 表面。请更新驱动程序或手动禁用 HDR 后重试；此截图无法保留 HDR。",L"グラフィックスドライバーが HDR ディスプレイに対して SDR サーフェスを返しました。ドライバーを更新するか HDR を明示的に無効にして再試行してください。この撮影では HDR を保持できません。",L"Der Grafiktreiber hat für einen HDR-Bildschirm eine SDR-Oberfläche geliefert. Aktualisieren Sie den Treiber oder deaktivieren Sie HDR vor dem erneuten Versuch ausdrücklich; diese Aufnahme kann HDR nicht erhalten.",L"El controlador gráfico devolvió una superficie SDR para una pantalla HDR. Actualiza el controlador o desactiva HDR antes de reintentar; esta captura no puede conservar HDR."}},
    {TextId::DesktopFormat,{L"The graphics driver returned an unsupported desktop format. Update the driver and retry.",L"Графический драйвер вернул неподдерживаемый формат рабочего стола. Обновите драйвер и повторите попытку.",L"显卡驱动程序返回了不支持的桌面格式。请更新驱动程序后重试。",L"グラフィックスドライバーが未対応のデスクトップ形式を返しました。ドライバーを更新して再試行してください。",L"Der Grafiktreiber hat ein nicht unterstütztes Desktop-Format geliefert. Aktualisieren Sie den Treiber und versuchen Sie es erneut.",L"El controlador gráfico devolvió un formato de escritorio no compatible. Actualiza el controlador e inténtalo de nuevo."}},
    {TextId::CaptureLayoutChanged,{L"The display layout changed during capture. Capture again.",L"Расположение экранов изменилось во время захвата. Сделайте снимок заново.",L"截图期间显示器布局已更改。请重新截图。",L"撮影中にディスプレイの配置が変わりました。もう一度撮影してください。",L"Die Bildschirmanordnung hat sich während der Aufnahme geändert. Nehmen Sie erneut auf.",L"La disposición de las pantallas cambió durante la captura. Vuelve a capturar."}},
    {TextId::NoDisplays,{L"No active displays are available for capture.",L"Нет активных экранов для захвата.",L"没有可供截图的活动显示器。",L"撮影できる有効なディスプレイがありません。",L"Es sind keine aktiven Bildschirme für die Aufnahme verfügbar.",L"No hay pantallas activas disponibles para capturar."}},
    {TextId::CaptureSettingsChanged,{L"Display settings changed during capture. Capture again.",L"Настройки экранов изменились во время захвата. Сделайте снимок заново.",L"截图期间显示设置已更改。请重新截图。",L"撮影中にディスプレイの設定が変わりました。もう一度撮影してください。",L"Die Bildschirmeinstellungen haben sich während der Aufnahme geändert. Nehmen Sie erneut auf.",L"La configuración de pantalla cambió durante la captura. Vuelve a capturar."}},
    {TextId::CaptureCanceled,{L"Capture canceled.",L"Захват отменён.",L"截图已取消。",L"撮影をキャンセルしました。",L"Aufnahme abgebrochen.",L"Captura cancelada."}},
    {TextId::CaptureTimeout,{L"Timed out waiting for a desktop image. Unlock Windows and capture again.",L"Истекло время ожидания изображения рабочего стола. Разблокируйте Windows и сделайте снимок заново.",L"等待桌面图像超时。请解锁 Windows 并重新截图。",L"デスクトップの画像を待機中にタイムアウトしました。Windows のロックを解除して、もう一度撮影してください。",L"Zeitüberschreitung beim Warten auf ein Desktop-Bild. Entsperren Sie Windows und nehmen Sie erneut auf.",L"Se agotó el tiempo de espera de la imagen del escritorio. Desbloquea Windows y captura de nuevo."}},
    {TextId::ProtectedContent,{L"Windows hid protected content in this capture. Close the protected window and capture again.",L"Windows скрыл защищённое содержимое на снимке. Закройте окно с защищённым содержимым и сделайте снимок заново.",L"Windows 在此次截图中隐藏了受保护的内容。请关闭受保护的窗口并重新截图。",L"Windows がこの撮影で保護されたコンテンツを隠しました。保護されたウィンドウを閉じて、もう一度撮影してください。",L"Windows hat geschützte Inhalte in dieser Aufnahme ausgeblendet. Schließen Sie das geschützte Fenster und nehmen Sie erneut auf.",L"Windows ocultó contenido protegido en esta captura. Cierra la ventana protegida y captura de nuevo."}},
    {TextId::UpdateStartupFailed,{L"The updated application did not start. Restoring the previous executable.",L"Обновлённое приложение не запустилось. Восстанавливается предыдущая версия.",L"更新后的应用程序未启动。正在恢复之前的可执行文件。",L"更新したアプリが起動しませんでした。以前の実行ファイルを復元しています。",L"Die aktualisierte Anwendung wurde nicht gestartet. Die vorherige ausführbare Datei wird wiederhergestellt.",L"La aplicación actualizada no se inició. Restaurando el ejecutable anterior."}},
    {TextId::DownloadHttpFailure,{L"The update download failed (HTTP {0}).",L"Не удалось загрузить обновление (HTTP {0}).",L"更新下载失败（HTTP {0}）。",L"更新のダウンロードに失敗しました（HTTP {0}）。",L"Der Update-Download ist fehlgeschlagen (HTTP {0}).",L"La descarga de la actualización falló (HTTP {0})."}},
    {TextId::HelperInitializeFailed,{L"The update helper could not initialize. ScreenshotTool will keep running.",L"Не удалось запустить помощник обновления. ScreenshotTool продолжит работу.",L"更新助手无法初始化。ScreenshotTool 将继续运行。",L"更新ヘルパーを初期化できませんでした。ScreenshotTool は引き続き動作します。",L"Der Update-Helfer konnte nicht initialisiert werden. ScreenshotTool läuft weiter.",L"No se pudo inicializar el asistente de actualización. ScreenshotTool seguirá funcionando."}},
    {TextId::ReplaceFailed,{L"Could not replace ScreenshotTool. The previous executable was retained.",L"Не удалось заменить ScreenshotTool. Предыдущий файл сохранён.",L"无法替换 ScreenshotTool。之前的可执行文件已保留。",L"ScreenshotTool を置き換えられませんでした。以前の実行ファイルは保持されています。",L"ScreenshotTool konnte nicht ersetzt werden. Die vorherige ausführbare Datei wurde beibehalten.",L"No se pudo reemplazar ScreenshotTool. Se conservó el ejecutable anterior."}},
    {TextId::BackupHelp,{L"Previous executable backup: {0}",L"Резервная копия предыдущего файла: {0}",L"之前的可执行文件备份：{0}",L"以前の実行ファイルのバックアップ：{0}",L"Sicherung der vorherigen ausführbaren Datei: {0}",L"Copia de seguridad del ejecutable anterior: {0}"}},
    {TextId::OperationFailed,{L"ScreenshotTool could not complete the operation. Try again.",L"Не удалось завершить операцию в ScreenshotTool. Повторите попытку.",L"ScreenshotTool 无法完成操作。请重试。",L"ScreenshotTool は操作を完了できませんでした。再試行してください。",L"ScreenshotTool konnte den Vorgang nicht abschließen. Versuchen Sie es erneut.",L"ScreenshotTool no pudo completar la operación. Inténtalo de nuevo."}},
    {TextId::SelectedImageTooLarge,{L"The selected image is too large. Select a smaller region.",L"Выбранное изображение слишком велико. Выберите меньшую область.",L"所选图像过大。请选择较小的区域。",L"選択した画像が大きすぎます。より小さい範囲を選んでください。",L"Das ausgewählte Bild ist zu groß. Wählen Sie einen kleineren Bereich.",L"La imagen seleccionada es demasiado grande. Selecciona una región más pequeña."}},
    {TextId::TooManyScreenshots,{L"Too many screenshots with this name.",L"Слишком много снимков с таким именем.",L"此名称的截图过多。",L"同じ名前のスクリーンショットが多すぎます。",L"Zu viele Screenshots mit diesem Namen.",L"Hay demasiadas capturas con este nombre."}},
    {TextId::SdrBrightnessChanged,{L"Windows SDR brightness changed. Press Print Screen to capture at the new brightness.",L"Яркость SDR в Windows изменилась. Нажмите Print Screen, чтобы сделать снимок с новой яркостью.",L"Windows SDR 亮度已更改。按 Print Screen 以新亮度截图。",L"Windows の SDR の明るさが変わりました。Print Screen を押して新しい明るさで撮影してください。",L"Die SDR-Helligkeit von Windows hat sich geändert. Drücken Sie Print Screen, um mit der neuen Helligkeit aufzunehmen.",L"El brillo SDR de Windows ha cambiado. Pulsa Print Screen para capturar con el nuevo brillo."}},
    {TextId::HdrModeChanged,{L"Windows HDR mode changed. Press Print Screen to capture with the new display settings.",L"Режим HDR в Windows изменился. Нажмите Print Screen, чтобы сделать снимок с новыми настройками экрана.",L"Windows HDR 模式已更改。按 Print Screen 使用新显示设置截图。",L"Windows の HDR モードが変わりました。Print Screen を押して新しいディスプレイ設定で撮影してください。",L"Der HDR-Modus von Windows hat sich geändert. Drücken Sie Print Screen, um mit den neuen Bildschirmeinstellungen aufzunehmen.",L"El modo HDR de Windows ha cambiado. Pulsa Print Screen para capturar con la nueva configuración de pantalla."}},
};

constexpr size_t entryCount=static_cast<size_t>(TextId::Count);
constexpr unsigned invalidMask=std::numeric_limits<unsigned>::max();
constexpr unsigned placeholders(std::wstring_view value) {
    unsigned mask{};
    for(size_t position=0;position<value.size();++position) {
        if(value[position]==L'}')return invalidMask;
        if(value[position]!=L'{')continue;
        ++position;
        if(position>=value.size() || value[position]<L'0' || value[position]>L'9')return invalidMask;
        unsigned argument{};
        do {
            argument=argument*10+static_cast<unsigned>(value[position]-L'0');
            if(argument>=31)return invalidMask;
            ++position;
        }while(position<value.size() && value[position]>=L'0' && value[position]<=L'9');
        if(position>=value.size() || value[position]!=L'}')return invalidMask;
        mask|=1u<<argument;
    }
    return mask;
}
constexpr bool validCatalog() {
    if constexpr(std::size(catalog)!=entryCount)return false;
    std::array<bool,entryCount> seen{};
    for(const auto& entry:catalog) {
        const auto id=static_cast<size_t>(entry.id);
        if(id>=entryCount || seen[id])return false;
        seen[id]=true;
        const auto expected=placeholders(entry.values[0]);
        if(expected==invalidMask || (expected&(expected+1u)))return false;
        for(const auto value:entry.values)if(value.empty() || placeholders(value)!=expected)return false;
    }
    return true;
}
static_assert(validCatalog(),"Every TextId needs six complete translations with matching contiguous placeholders.");

constexpr auto catalogIndex=[] {
    std::array<size_t,entryCount> result{};
    for(size_t i=0;i<std::size(catalog);++i)result[static_cast<size_t>(catalog[i].id)]=i;
    return result;
}();
size_t languageColumn(Language language) {
    if(language==Language::Automatic)language=systemLanguage();
    for(size_t i=0;i<Languages.size();++i)if(Languages[i]==language)return i;
    return 0;
}
std::wstring substitute(std::wstring_view pattern,const std::vector<std::wstring_view>& arguments) {
    const auto mask=placeholders(pattern);
    if(mask==invalidMask || arguments.size()>=31 || mask!=((1u<<arguments.size())-1u))
        throw std::invalid_argument("Localization arguments do not match the message template.");
    std::wstring result;
    result.reserve(pattern.size());
    for(size_t position=0;position<pattern.size();++position) {
        if(pattern[position]!=L'{'){result+=pattern[position];continue;}
        unsigned argument{};
        while(pattern[++position]!=L'}')argument=argument*10+static_cast<unsigned>(pattern[position]-L'0');
        // Insert argument bytes directly: braces in paths or diagnostics are data.
        result+=arguments[argument];
    }
    return result;
}
std::string utf8(std::wstring_view value) {
    if(value.empty())return {};
    const auto length=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(length<=0)return "ScreenshotTool error";
    std::string result(static_cast<size_t>(length),'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr);
    return result;
}
std::wstring fromUtf8(std::string_view value) {
    if(value.empty())return {};
    const auto length=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(length<=0)return L"Unknown error";
    std::wstring result(static_cast<size_t>(length),L'\0');
    MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length);
    return result;
}
int CALLBACK foundFont(const LOGFONTW*,const TEXTMETRICW*,DWORD,LPARAM found) {
    *reinterpret_cast<bool*>(found)=true;return 0;
}
bool availableFont(HDC dc,const wchar_t* family) {
    LOGFONTW font{};font.lfCharSet=DEFAULT_CHARSET;wcscpy_s(font.lfFaceName,family);
    bool found{};EnumFontFamiliesExW(dc,&font,foundFont,reinterpret_cast<LPARAM>(&found),0);return found;
}
bool fontCovers(HDC dc,HFONT font,std::wstring_view sample) {
    auto previous=SelectObject(dc,font);
    std::vector<WORD> glyphs(sample.size());
    const auto result=GetGlyphIndicesW(dc,sample.data(),static_cast<int>(sample.size()),glyphs.data(),GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc,previous);
    return result!=GDI_ERROR && std::none_of(glyphs.begin(),glyphs.end(),[](WORD glyph){return glyph==0xffff;});
}
bool highSurrogate(wchar_t value){return value>=0xd800 && value<=0xdbff;}
bool lowSurrogate(wchar_t value){return value>=0xdc00 && value<=0xdfff;}
}

Language resolveLanguage(Language preference,LANGID system) {
    if(preference!=Language::Automatic) {
        for(const auto language:Languages)if(language==preference)return language;
        return Language::English;
    }
    switch(PRIMARYLANGID(system)) {
    case LANG_RUSSIAN:return Language::Russian;
    case LANG_CHINESE:return Language::Chinese;
    case LANG_JAPANESE:return Language::Japanese;
    case LANG_GERMAN:return Language::German;
    case LANG_SPANISH:return Language::Spanish;
    default:return Language::English;
    }
}
Language systemLanguage(){return resolveLanguage(Language::Automatic,GetUserDefaultUILanguage());}
std::string_view languageTag(Language language) {
    switch(language) {
    case Language::English:return "en";
    case Language::Russian:return "ru";
    case Language::Chinese:return "zh-Hans";
    case Language::Japanese:return "ja";
    case Language::German:return "de";
    case Language::Spanish:return "es";
    default:return "auto";
    }
}
Language parseLanguage(std::string_view tag) {
    for(const auto language:Languages)if(languageTag(language)==tag)return language;
    return Language::Automatic;
}
const wchar_t* languageName(Language language) {
    switch(language) {
    case Language::English:return L"English";
    case Language::Russian:return L"Русский";
    case Language::Chinese:return L"简体中文";
    case Language::Japanese:return L"日本語";
    case Language::German:return L"Deutsch";
    case Language::Spanish:return L"Español";
    default:return text(systemLanguage(),TextId::Automatic).data();
    }
}
HFONT createUiFont(Language language,int pixelHeight,int weight) {
    language=resolveLanguage(language,GetUserDefaultUILanguage());
    LOGFONTW font{};
    font.lfHeight=pixelHeight>0?-pixelHeight:pixelHeight;
    font.lfWeight=weight;font.lfCharSet=DEFAULT_CHARSET;font.lfQuality=CLEARTYPE_QUALITY;
    std::array<const wchar_t*,6> candidates{L"Segoe UI",L"Tahoma",L"Arial",nullptr,nullptr,nullptr};
    std::wstring_view sample=L"ScreenshotTool";
    if(language==Language::Chinese) {
        candidates={L"Microsoft YaHei UI",L"Microsoft YaHei",L"Microsoft JhengHei UI",L"Microsoft JhengHei",L"SimSun",L"Segoe UI"};
        sample=L"简体中文截图";
    }else if(language==Language::Japanese) {
        candidates={L"Yu Gothic UI",L"Yu Gothic",L"Meiryo",L"Meiryo UI",L"MS Gothic",L"Segoe UI"};
        sample=L"日本語撮影";
    }else if(language==Language::Russian)sample=L"Русский";
    else if(language==Language::German)sample=L"Deutschäöüß";
    else if(language==Language::Spanish)sample=L"Españoláéíóú";
    HDC dc=GetDC(nullptr);
    if(dc) {
        for(const auto* family:candidates) {
            if(!family || !availableFont(dc,family))continue;
            wcscpy_s(font.lfFaceName,family);
            auto created=CreateFontIndirectW(&font);
            if(!created)continue;
            if(fontCovers(dc,created,sample)){ReleaseDC(nullptr,dc);return created;}
            DeleteObject(created);
        }
        ReleaseDC(nullptr,dc);
    }
    // Return an owned font even if none of the preferred families is installed.
    LOGFONTW fallback{};
    if(GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(fallback),&fallback))font=fallback;
    else wcscpy_s(font.lfFaceName,L"Segoe UI");
    font.lfHeight=pixelHeight>0?-pixelHeight:pixelHeight;
    font.lfWeight=weight;font.lfCharSet=DEFAULT_CHARSET;font.lfQuality=CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&font);
}
std::wstring_view text(Language language,TextId id) {
    const auto index=static_cast<size_t>(id);
    if(index>=entryCount)throw std::out_of_range("Invalid localization message ID.");
    return catalog[catalogIndex[index]].values[languageColumn(language)];
}
std::wstring format(Language language,TextId id,std::initializer_list<std::wstring_view> arguments) {
    return substitute(text(language,id),{arguments.begin(),arguments.end()});
}
std::wstring Message::render(Language language,TextId guidance) const {
    std::vector<std::wstring_view> values;values.reserve(arguments.size());
    for(const auto& argument:arguments)values.push_back(argument);
    auto result=substitute(text(language,id),values);
    if(guidance!=TextId::Count){result+=L"\n\n";result+=format(language,guidance);}
    if(!diagnostic.empty()){result+=L"\n\n";result+=diagnostic;}
    return result;
}
AppError::AppError(TextId id,std::vector<std::wstring> arguments)
    :std::runtime_error(utf8(Message{id,arguments,{}}.render(Language::English))),message_{id,std::move(arguments),{}} {}
Message errorMessage(const std::exception& error,TextId context) {
    if(const auto* localized=dynamic_cast<const AppError*>(&error))return localized->message();
    return {context,{},fromUtf8(error.what())};
}
std::wstring abbreviate(std::wstring_view value,size_t maximum) {
    if(value.size()<=maximum)return std::wstring(value);
    if(!maximum)return {};
    size_t length=maximum-1;
    if(length && length<value.size() && highSurrogate(value[length-1]) && lowSurrogate(value[length]))--length;
    std::wstring result(value.substr(0,length));result+=L'…';return result;
}
}
