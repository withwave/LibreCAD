# LibreCAD 웹앱(브라우저) 이식 타당성 분석 보고서

작성일: 2026-05-22
대상 코드베이스: `/Users/pss/AI/LibreCAD` (master, commit 5315f0ce8)
분석 방식: 실제 소스 트리 탐색 및 정량 측정 기반

---

## 0. 요약 (Executive Summary)

LibreCAD는 약 **29만 라인의 애플리케이션 코드**(`librecad/src`)와 **11만 라인의 벤더링 라이브러리**(`libraries/`)로 구성된 Qt6 기반 데스크톱 2D CAD 애플리케이션이다.

핵심 결론:

- **지오메트리/수학/엔티티 코어는 비교적 프레임워크 독립적**이다. 다만 Qt의 `QString`/`QList` 같은 **비-GUI Qt(Qt Core) 타입에 광범위하게 의존**하며, 엔티티의 `draw()` 메서드가 `RS_Painter`(=`QPainter` 상속)에 직접 묶여 있어 "순수 C++"는 아니다.
- **GUI/액션/이벤트 계층은 Qt Widgets에 깊게 결합**되어 있다. `QWidget` 561회, `QAction` 600회, `QMouseEvent` 211회, `connect()` 호출 1030회, `Q_OBJECT` 매크로 459개 파일에서 사용.
- 가장 현실적인 경로는 **(전략 A) Qt for WebAssembly로 전체 앱을 거의 그대로 wasm 빌드**하는 것이며, 빠른 MVP 확보가 가능하다. 다만 성능/번들 크기/파일 접근/DWG 제약이 따른다.
- 장기적으로 진정한 "웹앱 UX"를 원하면 **(전략 B) C++ 지오메트리·파서 코어를 emscripten으로 wasm 컴파일 + 신규 TS/Canvas 프런트엔드**가 최선의 균형점이다.
- **(전략 C) 순수 TypeScript 전면 재작성**은 DXF/DWG 파서와 30만 라인 로직을 버리는 셈이라 비용·리스크가 가장 크다.

권장: **전략 A로 PoC/MVP를 빠르게 확보**한 뒤, 제품화 단계에서 **전략 B로 점진 전환**하는 하이브리드 로드맵.

---

## 1. 아키텍처 조사 (정량적)

### 1.1 디렉터리 구조

```
librecad/src/
  actions/      드로잉/편집/줌/스냅 등 사용자 액션 (GUI 결합)
  cmd/          커맨드라인 인터페이스
  lib/
    engine/     엔티티 모델 + 문서 모델 (코어)
    gui/        뷰포트/렌더러/이벤트 핸들러 추상화
    fileio/     파일 입출력 진입점
    filters/    DXF/CXF/JWW/LFF 등 포맷 필터
    math/       순수 수학 (벡터/2차곡선/선분 계산)
    modification/ 트림/오프셋/이동 등 형상 편집 연산
    creation/   엔티티 생성 헬퍼
    information/ 측정/조회
    generators/ SVG/이미지/레이어 출력
    printing/   인쇄
    scripting/  스크립팅
  ui/           Qt 위젯/다이얼로그/도킹/메인윈도우 (GUI)
  main/         앱 진입점, console_dxf2pdf(헤드리스 도구)
  plugins/      플러그인 인터페이스
libraries/
  libdxfrw/     DXF/DWG 읽기·쓰기 (벤더링)
  jwwlib/       JWW(일본 CAD) 읽기
  muparser/     수식 파서
  lciconengine/ 아이콘 엔진
plugins/        align/gear/list 등 외부 플러그인 (Qt 의존)
```

### 1.2 서브시스템별 규모 (파일 수 / LOC)

| 서브시스템 | 파일 | LOC | 분류 |
|---|---|---|---|
| `lib/engine` (엔티티+문서 모델) | 247 | 68,339 | 코어 (Qt Core 의존) |
| `lib/math` | 12 | 4,487 | **순수 수학** |
| `lib/modification` | 8 | 5,159 | 코어 형상 편집 |
| `lib/creation` | 2 | 1,300 | 코어 |
| `lib/information` | 6 | 1,609 | 코어 |
| `lib/filters` (포맷) | 27 | 26,509 | 파일 I/O |
| `lib/generators` | 9 | 1,866 | 출력 |
| `lib/gui` (뷰포트/렌더러) | 52 | 15,215 | GUI 추상화 |
| `lib/printing` | 2 | 394 | 인쇄 |
| `lib/scripting` | 10 | 1,431 | 스크립팅 |
| `lib/actions` (액션 기반클래스) | 18 | 4,863 | GUI 결합 |
| `actions` (구체 액션 217개) | 434 | 62,719 | **GUI 결합** |
| `ui` (위젯/다이얼로그) | 549 | 86,999 | **GUI 결합 (Qt Widgets)** |
| `cmd` | 3 | 1,818 | CLI |
| `main` | 18 | 4,174 | 진입점 |
| `plugins` (내부) | 10 | 1,329 | GUI 결합 |
| **합계 (src)** | | **≈290,303** | |

벤더링 라이브러리:

| 라이브러리 | 파일 | LOC | 비고 |
|---|---|---|---|
| `libdxfrw` | 55 | 90,381 | DXF/DWG R/W, 표준 C++ `fstream` 사용 |
| `jwwlib` | 15 | 13,713 | JWW 읽기 |
| `muparser` | 20 | 9,432 | 수식 파서, 순수 C++ |
| `lciconengine` | 3 | 643 | Qt 아이콘 |

추가로 `.ui` 파일(Qt Designer 폼) **162개** — 전부 Qt Widgets 전용 UI 정의.

### 1.3 주요 서브시스템 식별

- **엔티티 모델 (RS_Entity 계층)**: `librecad/src/lib/engine/document/entities/`
  - `rs_entity.h/.cpp`, `rs_line`, `rs_arc`, `rs_circle`, `rs_ellipse`, `rs_polyline`, `rs_spline`, `rs_text`, `rs_dimension`, `rs_insert` 등
  - 컨테이너: `rs_entitycontainer.{h,cpp}`, 문서: `rs_document`, `rs_graphic`
  - 모든 엔티티는 `LC_Drawable` 인터페이스(`librecad/src/lib/engine/lc_drawable.h`)를 구현하며 `virtual void draw(RS_Painter *painter)=0`를 가짐.
- **지오메트리/수학 코어**: `lib/math` + `lib/engine/rs_vector`, `lib/modification`. 가장 이식성 높음.
- **파일 I/O**: `lib/filters/rs_filterdxfrw.cpp`(libdxfrw 어댑터), `rs_filterjww`, `rs_filtercxf`, `rs_filterlff` + `libraries/libdxfrw`.
- **렌더링 엔진**: `lib/gui/render/`
  - 추상 렌더러: `lc_graphicviewportrenderer.{h,cpp}` — `renderEntity(RS_Painter*, RS_Entity*)`가 내부에서 `e->draw(painter)` 호출 (`lc_graphicviewportrenderer.cpp:124`).
  - 위젯 구현: `render/widget/lc_widgetviewportrenderer`, `lc_graphicviewrenderer`
  - **헤드리스 구현 존재**: `render/headless/lc_printviewportrenderer` (인쇄/PDF용)
  - 페인터: `render/rs_painter.{h,cpp}`.
- **GUI 계층**: `librecad/src/ui/` (메인윈도우, 도킹, 다이얼로그) — Qt Widgets.
- **액션/커맨드 시스템**: `lib/actions/rs_actioninterface.h` (`QObject` 상속), `actions/` 하위 217개 구체 액션.
- **이벤트 처리**: `lib/gui/lc_eventhandler.h`, `rs_eventhandler.h`, `QMouseEvent`/`QKeyEvent` 기반.

---

## 2. Qt 결합도 분석

### 2.1 (a) 비교적 프레임워크 독립적인 코어

- `lib/math` 12개 파일 중 Qt 타입 사용 파일은 **5개뿐**(주로 `RS_Vector` 연동). 순수 알고리즘 비중 높음.
- `libraries/libdxfrw`, `libraries/muparser`, `libraries/jwwlib`는 **Qt 비의존 표준 C++**. libdxfrw는 `std::ifstream/ofstream`(`libraries/libdxfrw/src/libdxfrw.cpp` 등)을 사용 — wasm 가상 파일시스템(MEMFS/IDBFS)으로 매핑 가능.
- `lib/modification`, `lib/creation`, `lib/information`도 알고리즘 중심.

### 2.2 (a') 코어이지만 Qt Core(비-GUI)에 의존

`lib/engine`(247파일) 중 Qt를 참조하는 파일은 **66개**. 단, 대부분은 **GUI가 아닌 Qt Core 타입**이다. 엔진 내 Qt 클래스 사용 빈도:

```
QString    1176    QList      120    QStringList 69
QPainterPath 53    QColor      33    QPointF     29
QFileInfo   15     QFile       11    QImage      10
QPolygonF    9     QFont        8    QObject     94
```

핵심 시사점:
- `QString`/`QList`/`QStringList` 의존은 wasm(전략 A)에선 무해하고, 전략 B에선 `std::string`/`std::vector`로 치환 가능하나 **1,000회 이상 등장**해 기계적 치환 비용이 적지 않다.
- **`QPainterPath`(53), `QColor`(33), `QPointF`(29), `QFont`(8)** 가 엔진까지 침투해 있다. 즉 엔티티 코어가 그리기/렌더 자료형과 약하게 결합됨. `RS_Painter`는 `QPainter`를 **상속**(`rs_painter.h:67` `class RS_Painter: public QPainter`)하고, 모든 엔티티의 `draw()`가 이 타입을 인자로 받는다. → **렌더링 추상화가 Qt에서 완전히 분리되어 있지 않다.**

### 2.3 (b) Qt GUI에 강하게 결합된 코드

전체 `src` 기준 GUI 클래스 등장 빈도:

```
QAction 600   QWidget 561   QPainter 246   QMouseEvent 211
QMenu 180     QPainterPath 151  QActionGroup 105  QDockWidget 94
QApplication 83  QKeyEvent 82  QDialog 65    QMenuBar 34
```

- `Q_OBJECT` 매크로: **459개 파일** → 시그널/슬롯 메타오브젝트 시스템에 깊게 의존.
- `connect()` 호출: **1,030회**.
- `.ui` 폼: **162개** (Qt Widgets 전용).
- 액션 기반클래스 `RS_ActionInterface`가 `QObject` 상속(`rs_actioninterface.h:60-66`), 217개 구체 액션이 마우스/키 이벤트로 동작.

→ `ui/`(87K LOC), `actions/`(63K LOC), `lib/actions`, `lib/gui` 위젯 구현부 합계 **약 17만 LOC가 Qt Widgets에 직접 결합**. 이 부분이 이식의 핵심 병목.

### 2.4 긍정적 신호 (아키텍처 분리 흔적)

- **다이얼로그 추상화**: `RS_DialogFactoryInterface`(`lib/gui/rs_dialogfactoryinterface.h`)가 순수 가상 인터페이스로 UI를 코어에서 분리. 다른 프런트엔드 주입 여지 존재.
- **렌더러 추상화**: `LC_GraphicViewportRenderer`가 추상 기반클래스이고, 위젯/헤드리스 구현이 분리됨. 신규 백엔드(예: Canvas/WebGL) 추가 지점이 명확.
- **헤드리스 PDF 도구**: `librecad/src/main/console_dxf2pdf/`는 이미 GUI 없이 DXF→PDF 변환을 수행 — 코어가 GUI 없이도 상당 부분 동작 가능함을 입증.
- **레거시 QGraphicsView 미사용**: `QGraphicsView`/`QGraphicsScene`은 옵션 다이얼로그 1곳에서만 사용. 메인 캔버스는 자체 뷰포트+`QPainter` 렌더링 → 표준 Qt 렌더로 wasm 호환성 양호.
- **외부 의존성 부담 적음**: Boost는 12개 파일에서만(`numeric`, `math`, `geometry` 등 헤더온리 위주) 사용 — wasm 빌드 가능. Freetype은 **별도 `ttf2lff` 폰트 변환 도구 전용**(`CMakeLists.txt:1693`)이라 본체 wasm 빌드에서 제외 가능. 스레드 사용은 단 2개 파일 — wasm 단일 스레드 제약에 유리.

---

## 3. 웹앱 이식 전략 비교

### 전략 A — Qt for WebAssembly (기존 앱 거의 그대로 wasm 빌드)

Qt6은 공식적으로 `wasm_*` 플랫폼을 지원하며, `QWidget`/`QPainter` 앱을 wasm으로 컴파일해 `<canvas>`에서 구동 가능하다. 현재 코드베이스의 Qt 모듈은 `Core, Gui, Widgets, PrintSupport, Svg, Network`(`CMakeLists.txt:14`).

가능성:
- 메인 캔버스가 `QPainter` 기반 자체 렌더이므로 **wasm Qt에서 그대로 동작 가능성 높음.**
- libdxfrw/muparser/jwwlib가 표준 C++ → 컴파일 가능.
- 162개 `.ui` 다이얼로그, 액션 시스템도 Qt가 wasm에서 에뮬레이트.

제약/리스크:
- **파일 시스템**: 브라우저는 로컬 FS 직접 접근 불가. `QFile`/`QFileDialog`(63개 파일에서 사용)는 wasm에서 가상 FS/다운로드·업로드 다이얼로그로 대체되며, 다중 파일/외부 참조(xref, 이미지 인서트) 처리가 까다로움.
- **번들 크기**: 29만+11만 LOC + Qt Widgets 런타임 → wasm 바이너리가 수십 MB 단위로 커질 수 있어 초기 로딩이 느림.
- **PrintSupport/PDF**: 인쇄·PDF 출력은 wasm에서 제한적. `console_dxf2pdf` 경로를 클라이언트 PDF 생성으로 재설계 필요.
- **DWG**: libdxfrw의 DWG 리더(`dwgreader*`)는 컴파일은 되나, 복잡한 바이너리 파싱이 wasm에서 성능·메모리 부담.
- **성능**: 대형 도면(수십만 엔티티) 렌더가 wasm Qt 소프트웨어 래스터라이저에서 데스크톱 대비 느림.
- **UX**: Qt Widgets UI를 그대로 띄우는 형태라 "네이티브 웹앱" 느낌이 아니라 캔버스 안에 데스크톱 UI가 박힌 형태.
- **스레딩**: wasm 스레드는 SharedArrayBuffer + COOP/COEP 헤더 필요. 다행히 본 코드의 스레드 사용은 2파일뿐.

상대 공수: **소~중**. PoC는 수 주~수 개월. 단, 완전 동작·최적화까지는 추가 노력.

### 전략 B — C++ 코어를 emscripten wasm + 신규 JS/TS·Canvas/WebGL 프런트엔드

`lib/engine`(엔티티/문서) + `lib/math` + `lib/modification` + `lib/filters` + `libdxfrw`/`muparser`를 emscripten으로 wasm 라이브러리화하고, 그리기·UI·이벤트는 신규 웹 프런트엔드(TypeScript + Canvas2D 또는 WebGL/PixiJS)로 구현.

가능성/근거:
- 코어가 이미 `LC_GraphicViewportRenderer`/`RS_DialogFactoryInterface`로 추상화되어 있어 **신규 렌더러·다이얼로그 백엔드 주입 지점이 명확**.
- 헤드리스(`console_dxf2pdf`) 경로 존재가 "코어 단독 동작" 가능성을 증명.
- 렌더 모델 전환: 엔티티의 `draw(RS_Painter*)`를 그대로 쓰는 대신, 엔티티를 **순수 기하 프리미티브(선/호/베지어/폴리라인) 디스플레이 리스트로 직렬화**해 JS로 넘기고 Canvas/WebGL에서 그리는 방식이 권장(파서·기하 계산은 wasm, 렌더는 JS).

도전 과제:
- 엔진의 `QString`/`QList`/`QColor`/`QPainterPath` 의존(2.2 참조)을 wasm 경계에서 정리해야 함. `QString`만 1,000회 이상 → Qt Core를 wasm에 포함시키거나(번들 증가) `std::string`으로 치환(작업량 큼) 중 택일.
- 217개 액션(63K LOC)과 87K LOC UI는 **재사용 불가, 신규 작성** 대상. 인터랙티브 드로잉 도구 로직 일부는 코어로 끌어내려 재활용 가능.

상대 공수: **중~대**. 그러나 결과물의 웹 UX·성능·유지보수성이 가장 우수.

### 전략 C — 순수 TypeScript 전면 재작성

LibreCAD를 참고 자료로만 두고 처음부터 웹 CAD 라이브러리(예: 기존 JS DXF 뷰어/캔버스 엔진)로 재작성.

평가:
- **DXF/DWG 파싱(특히 DWG 바이너리)** 을 JS로 재구현하는 비용이 막대하고 호환성 리스크 큼. libdxfrw 9만 LOC의 성숙도를 버리는 셈.
- 기하 알고리즘(스플라인, 2차곡선 교점, 디멘션, 해치)도 재작성.
- 장점은 순수 웹 스택·최적 UX·작은 번들. 단 **총비용·리스크 최대**, 기존 자산 활용 0에 가까움.

상대 공수: **최대**. 별도 신규 제품 개발에 가까움.

### 요약 비교표

| 항목 | A: Qt-wasm 전체 | B: C++코어 wasm + 웹 UI | C: 순수 TS 재작성 |
|---|---|---|---|
| 기존 코드 재사용 | 거의 전부 | 코어/파서(~40%) | 거의 없음 |
| 초기 MVP 속도 | 빠름 | 중간 | 느림 |
| 웹 UX 품질 | 낮음(데스크톱 UI 이식) | 높음 | 최고 |
| 번들 크기 | 큼(Qt 런타임 포함) | 중간 | 작음 |
| 렌더 성능 | 중(SW 래스터) | 높음(WebGL 가능) | 높음 |
| DXF/DWG 호환성 | 높음(libdxfrw 그대로) | 높음(libdxfrw 그대로) | 낮음(재구현) |
| 총 공수/리스크 | 소~중 | 중~대 | 최대 |

---

## 4. 공수·리스크 평가 및 MVP 정의

### 주요 기술 리스크

1. **렌더링 성능**: 대형 도면에서 wasm 소프트웨어 렌더(전략 A) 한계 → 전략 B의 WebGL/디스플레이리스트가 완화책. 뷰포트 컬링/클리핑은 이미 `getBoundingClipRect()` 등 존재.
2. **DWG 파싱**: libdxfrw DWG 리더의 wasm 동작·메모리. DWG는 MVP에서 제외하고 DXF 우선 권장.
3. **대형 파일/메모리**: wasm 32비트 힙(기본 ~2GB) 한계. 대형 DXF의 엔티티 컨테이너 메모리 관리 검증 필요.
4. **파일 영속성**: 로컬 FS 부재 → 업로드/다운로드 + IndexedDB(IDBFS) 또는 클라우드 저장. xref/이미지 인서트 등 외부 참조 처리 설계 필요.
5. **인쇄/PDF**: PrintSupport 제약 → 클라이언트 PDF 라이브러리 또는 서버 측 `console_dxf2pdf` 재활용.
6. **폰트/텍스트**: `QFont`/BiDi 텍스트(`rs_text_bidi_tests.cpp` 존재) — 웹 폰트로 매핑 필요. LFF 폰트(`rs_filterlff`)는 자체 처리 가능.

### MVP 범위 제안

- **MVP-0 (뷰어)**: DXF 업로드 → 파싱 → 캔버스에 표시 + 팬/줌/맞춤. 선/호/원/폴리라인/텍스트/인서트 렌더. 레이어 on/off.
- **MVP-1 (기본 편집)**: 선/원/호/폴리라인 그리기, 스냅, 선택/삭제/이동/복사, 실행취소(엔진에 `undo/` 모듈 존재).
- 이후 디멘션/해치/스플라인 편집, DXF 저장, DWG 읽기, 인쇄로 확장.

---

## 5. 권장안 및 단계별 로드맵

### 권장: 하이브리드 (A로 검증 → B로 제품화)

이유: 전략 A는 기존 자산을 최대한 활용해 **빠르게 동작하는 PoC**를 만들 수 있고, 동시에 코어의 wasm 컴파일 가능성·성능 한계를 실측할 수 있다. 이후 UX·성능이 중요한 부분만 전략 B 방식(코어 wasm + 웹 UI)으로 점진 대체한다. 전략 C는 권장하지 않는다(libdxfrw 등 핵심 자산 폐기 비용 과다).

### 단계별 로드맵

- **Phase 0 — 컴파일 타당성 검증 (수 주)**
  - Qt6 wasm 툴체인 구성. `console_dxf2pdf` 헤드리스 코어 + libdxfrw/muparser를 먼저 wasm로 빌드해 DXF 파싱이 브라우저에서 동작하는지 확인.
  - Boost 헤더온리·Freetype 분리(본체 제외) 확인.

- **Phase 1 — 뷰어 MVP**
  - 전략 A: 전체 앱 wasm 빌드로 빠른 뷰어, 또는
  - 전략 B: 코어 wasm + 신규 TS/Canvas 뷰어. 엔티티를 기하 프리미티브로 직렬화해 렌더.
  - 결과물: 브라우저에서 DXF 업로드·표시·팬/줌, 레이어 토글.

- **Phase 2 — 코어 편집**
  - 그리기(선/원/호/폴리라인), 스냅, 선택/이동/복사/삭제, undo/redo, DXF 저장(다운로드).
  - 전략 B로 전환 시 액션 로직 중 재사용 가능한 부분을 코어로 이관.

- **Phase 3 — 풀 기능**
  - 디멘션/해치/스플라인/텍스트(BiDi), 블록/인서트, DWG 읽기, 인쇄/PDF(클라이언트 또는 서버), 파일 영속성(클라우드/IndexedDB).
  - 성능 최적화: WebGL 렌더 백엔드, 뷰포트 컬링, 대형 도면 메모리 관리.

### 핵심 활용 포인트 (코드 근거)

- 렌더러 추상화 `LC_GraphicViewportRenderer`(`lib/gui/render/lc_graphicviewportrenderer.h`)에 **웹 렌더 백엔드 추가**.
- 다이얼로그 추상화 `RS_DialogFactoryInterface`(`lib/gui/rs_dialogfactoryinterface.h`)에 **웹 UI 구현 주입**.
- 헤드리스 경로 `main/console_dxf2pdf/`를 **코어 wasm 진입점 템플릿**으로 활용.
- 파서 `libraries/libdxfrw`(표준 C++/`fstream`)는 **거의 무변경 wasm 컴파일** 후 가상 FS 연결.

---

## 부록: 측정 명령 근거

- 서브시스템 LOC/파일 수: `find ... -name '*.cpp' -o -name '*.h' | xargs cat | wc -l`
- Qt 클래스 빈도: `grep -rho 'Q[A-Za-z]*' ... | sort | uniq -c | sort -rn`
- `Q_OBJECT` 459파일, `connect()` 1030회, `.ui` 162개, 액션 217개(`actions/**.cpp`)
- Boost 12파일, Freetype은 `ttf2lff` 전용(`CMakeLists.txt:1693`), 스레드 사용 2파일
- Qt 모듈: `CMakeLists.txt:14` (`Gui Core Widgets PrintSupport Svg Network`)
