# Inspector Visual Parity Scorecard (Automated HTML vs C++ Verification)

| Card | Tab | Verdict | Full SSIM | Body SSIM | Edge F1 | MAE | 4-Panel Proof |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| HeightFog | 1-Overview | **PASS** | `0.8816` | `0.8919` | `0.8774` | `0.0137` | `HeightFog-01-Overview-Proof.png` |
| HeightFog | 2-Density | **PASS** | `0.8638` | `0.8755` | `0.9301` | `0.0158` | `HeightFog-02-Density-Proof.png` |
| HeightFog | 3-Light | **PASS** | `0.9133` | `0.9337` | `0.9172` | `0.0108` | `HeightFog-03-Light-Proof.png` |
| AtmosphericFog | 1-Overview | **PASS** | `0.8687` | `0.8835` | `0.9290` | `0.0147` | `AtmosphericFog-01-Overview-Proof.png` |
| AtmosphericFog | 2-Spectrum | **PASS** | `0.8560` | `0.8653` | `0.9162` | `0.0168` | `AtmosphericFog-02-Spectrum-Proof.png` |
| LocalFog | 1-Overview | **PASS** | `0.8600` | `0.8597` | `0.8954` | `0.0150` | `LocalFog-01-Overview-Proof.png` |
| LocalFog | 2-Medium | **PASS** | `0.8952` | `0.9047` | `0.9168` | `0.0122` | `LocalFog-02-Medium-Proof.png` |
| LocalFog | 3-Shape | **PASS** | `0.8719` | `0.8762` | `0.8586` | `0.0145` | `LocalFog-03-Shape-Proof.png` |
| Wind | 1-Overview | **FAIL** | `0.8395` | `0.8316` | `0.8260` | `0.0182` | `Wind-01-Overview-Proof.png` |
| Wind | 2-Air | **PASS** | `0.8800` | `0.8809` | `0.9160` | `0.0122` | `Wind-02-Air-Proof.png` |
| Cloud | 1-Overview | **FAIL** | `0.7983` | `0.7747` | `0.5657` | `0.0220` | `Cloud-01-Overview-Proof.png` |
| Cloud | 2-Coverage | **PASS** | `0.8774` | `0.8674` | `0.7740` | `0.0153` | `Cloud-02-Coverage-Proof.png` |
| Cloud | 3-Layer | **FAIL** | `0.8450` | `0.8292` | `0.8782` | `0.0181` | `Cloud-03-Layer-Proof.png` |
| Cloud | 4-Body | **PASS** | `0.8548` | `0.8508` | `0.8616` | `0.0158` | `Cloud-04-Body-Proof.png` |
| Cloud | 5-Wind | **PASS** | `0.8890` | `0.9033` | `0.9030` | `0.0106` | `Cloud-05-Wind-Proof.png` |
| Cloud | 6-Quality | **PASS** | `0.9223` | `0.9357` | `0.8760` | `0.0093` | `Cloud-06-Quality-Proof.png` |
| LocalCloud | 1-Overview | **FAIL** | `0.7476` | `0.7230` | `0.7384` | `0.0311` | `LocalCloud-01-Overview-Proof.png` |
| LocalCloud | 2-Coverage | **PASS** | `0.8652` | `0.8633` | `0.7804` | `0.0199` | `LocalCloud-02-Coverage-Proof.png` |
| LocalCloud | 3-Layer | **FAIL** | `0.8398` | `0.8386` | `0.8791` | `0.0185` | `LocalCloud-03-Layer-Proof.png` |
| LocalCloud | 4-Body | **PASS** | `0.9120` | `0.9334` | `0.8916` | `0.0098` | `LocalCloud-04-Body-Proof.png` |
