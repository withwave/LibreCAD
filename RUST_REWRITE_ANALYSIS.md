# 러스트 전면 재작성 → WASM 웹앱 배포 방식 검토

> ⚠️ 기술·라이선스 검토 자료이며 법률 자문이 아님. 실제 진행 전 IP 변호사 검토 필수.

LibreCAD(C++/Qt/GPL)를 **참고만** 하여 Rust로 처음부터 새로 만들고 WebAssembly로 브라우저에 배포하는 방식.
앞선 `WEB_APP_FEASIBILITY.md`의 "전략 C(순수 재작성)"의 구체화 + Rust 특화 버전이다.

## 1. 가장 큰 장점 — 라이선스 해방 (단, "클린룸"이 전제)

| 항목 | Qt-WASM(전략 A) | Rust 재작성 |
|---|---|---|
| 코드 소유권 | LibreCAD GPL에 종속 | **우리가 100% 소유** |
| 독점/비공개화 | 불가 | **가능** (원하는 라이선스 선택) |
| Qt 라이선스 함정 | GPLv3 or 상용 | **해당 없음** (Qt 미사용) |
| 소스 공개 의무 | 전체 공개 | **없음** |

**핵심 원리:** 저작권은 "표현(소스 코드)"을 보호하지, **아이디어·알고리즘·파일 포맷**은 보호하지 않는다.
따라서 다음은 자유롭게 참고·재구현 가능:
- DXF/DWG **파일 포맷**(Autodesk가 규격 공개) — 재구현 합법
- 오프셋/트림/스냅/해치 등 **기하 알고리즘** (수학은 비저작권)
- 엔티티 모델 **설계**, UI 레이아웃, 명령 체계(217개 액션)를 **기능 명세**로 활용
- 🎁 LibreCAD 아이콘(`res/icons`)은 **CC0(퍼블릭 도메인)** → 독점 제품에도 그대로 재사용 가능

**⚠️ 절대 금지선 (이걸 어기면 GPL 파생물이 됨):**
- GPL 소스를 **줄 단위로 Rust로 번역(transliteration)** → 파생 저작물 → GPL 전염
- 안전책 = **클린룸 방식**: (a) 한 사람이 LibreCAD 동작/포맷을 읽고 *명세 문서*를 작성 →
  (b) 다른 사람이 그 명세만 보고 Rust로 독립 구현. 최소한 "코드 복붙/직역 금지" 원칙은 엄수.

## 2. 기술적 실현성 — Rust+WASM 생태계는 충분히 성숙

| LibreCAD 서브시스템 | Rust 대체 | 성숙도 |
|---|---|---|
| 렌더링 (`RS_Painter`=QPainter, CPU) | **wgpu** (WebGPU/WebGL2, GPU 가속) | ◎ 오히려 성능 우위 |
| GUI (Qt Widgets 17만 LOC) | **egui** (즉시모드, 네이티브+웹 동시) | ○ CAD 도구바/도크 구현 가능 |
| 경로/곡선 | **kurbo**(베지어), **lyon**(테셀레이션) | ◎ |
| 선형대수/변환 | **glam** / **nalgebra** | ◎ |
| 2D 기하 연산 | **geo**, **parry2d** | ○ |
| DXF 읽기/쓰기 | **`dxf`**(MIT, 성숙) / **`dxf-tools-rs`**(고성능) | ◎ |
| DWG | **`acadrust`**(DWG R13–R2018 주장) ⚠️라이선스·완성도 검증 필요 | △ 최대 리스크 |
| 폰트/텍스트 | **cosmic-text** / **ab_glyph** | ◎ |
| PDF/SVG 출력 | **printpdf** / 자체 SVG | ○ |
| WASM 빌드 | `wasm-bindgen`+`trunk`/`wasm-pack`, 1급 지원 | ◎ |

실제 선례: Hy-CAD(wgpu CAD 뷰어), easycad(Rust+wgpu+egui 2D CAD), cad-engine(Rust+WASM+TS+React) 등
이미 동일 스택으로 CAD 앱이 만들어지고 있다.

## 3. DWG = 유일한 진짜 난제

DWG는 비공개 포맷이라 어디서든 어렵다.
- `acadrust`가 순수 Rust DWG를 표방 → 사실이면 게임체인저. **단 라이선스/실제 호환성 검증 필수.**
- 대안: LibreDWG(**GPLv3** → 전염, 독점 불가) / ODA SDK(**상용 유료**) / **DWG 미지원하고 DXF만**.
- 권고: **DWG는 인터페이스 뒤로 격리**해 두고 MVP는 DXF만. 나중에 크레이트/상용 SDK를 갈아끼울 수 있게.

## 4. 비용·리스크

- **이것이 세 전략 중 공수 최대.** 원본은 ~290K LOC C++ 앱. 단,
  - 100% 기능 패리티는 MVP에 불필요, 현대 크레이트가 렌더/수학/파싱을 대거 처리,
  - Rust는 표현이 간결하고 메모리 안전 → 장기 유지보수·협업기능에 유리.
- 현실적 추정: 풀 패리티는 숙련팀 다분기(多분기) 과제. **MVP(뷰어+기본 작도)는 소수 정예로 수개월** 수준.
- 주요 리스크: ① CAD 도메인 깊이(치수·해치·구속·스플라인·블록), ② DWG, ③ 대형 도면 브라우저 성능,
  ④ 파일 영속/협업, ⑤ 기성 Rust "CAD 프레임워크" 부재(앱 셸은 직접 구축).

## 5. 권장 로드맵 (단계적)

- **Phase 0 — 파이프라인 증명:** `dxf` 크레이트로 파싱 → wgpu 렌더 → 팬/줌. (IP 청정·WASM 동작 검증)
- **Phase 1 — 기본 작도:** 선/원/호/폴리라인 + 스냅 + 레이어 + egui 도구바.
- **Phase 2 — 편집:** 트림/오프셋/이동/복사/미러, 치수, 해치, 텍스트, 블록.
- **Phase 3 — 제품화:** 스플라인/구속, PDF·SVG 출력, DWG(격리 모듈), 클라우드 저장·실시간 협업.

각 단계에서 **사용 크레이트 라이선스를 MIT/Apache로 통일**하고, GPL 크레이트(LibreDWG 등)는
독점 코어에 링크하지 않도록 분리한다.

## 6. 세 전략 최종 비교

| 기준 | A. Qt-WASM | B. C++코어+TS | **C. Rust 재작성** |
|---|---|---|---|
| 공수 | 최소 | 중 | **최대** |
| IP/독점화 | 불가(GPLv3) | 코어 GPL | **완전 자유** ✅ |
| Qt 라이선스 | 종속 | 부분 종속 | **무관** ✅ |
| 성능(브라우저) | 보통 | 보통~좋음 | **최상**(wgpu GPU) ✅ |
| DXF/DWG 자산 | 재사용 | 재사용(GPL) | 재구현/크레이트 |
| 장기 유지보수 | Qt 의존 | C++/JS 혼재 | **단일 Rust, 안전** ✅ |

## 7. 결론

**상업적·독점 웹 제품이 목표라면 Rust 전면 재작성이 사실상 정답에 가깝다.**
- 유일하게 GPL/Qt 라이선스에서 완전히 자유로운 길이며(클린룸 전제), 동시에 순수 TS 재작성보다
  연산 집약적 기하 코어에서 성능·안전성이 월등하다.
- 대가는 **최대 공수**와 **DWG 난제**. → "DXF 전용 MVP 먼저, DWG는 격리 후 추후" 전략으로 리스크 분산.
- 필수 가드레일: **클린룸 프로세스**, **크레이트 라이선스 MIT/Apache 통일**, **DWG 모듈 격리**, **출시 전 변호사 검토**.

---
### 출처
- https://crates.io/crates/dxf  (dxf-rs, MIT)
- https://lib.rs/crates/dxf-tools-rs
- https://docs.rs/crate/acadrust/latest  (DWG 표방 — 검증 필요)
- https://github.com/LibreDWG  (GPLv3)
- https://wgpu.rs/  ·  https://github.com/emilk/egui
- https://dev.to/zouyugangian/building-hy-cad-a-webgpu-powered-cad-viewer-with-rust-and-kiro-n82
- https://github.com/kristof1345/easycad  ·  https://github.com/Abhayrkhot/cad-engine
- 코드베이스 실측: `librecad/src/**`, `licenses/readme.md`(아이콘 CC0)
