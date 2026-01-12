# 🐧 Linux Kernel Device Driver Mini Project  
### RTC · 온습도 · 로터리 엔코더 기반 시계 / 환경 표시 장치

본 프로젝트는 **Linux Kernel Device Driver**를 직접 구현하여  
**RTC, 온습도 센서, 로터리 엔코더** 등 여러 하드웨어 장치를  
하나의 **Character Device Interface**로 통합한 임베디드 리눅스 프로젝트입니다.

커널 영역은 **하드웨어 제어 및 데이터 수집**에 집중하고,  
시간 계산·보정 및 출력 로직은 **유저 애플리케이션 영역**에서 처리하도록  
**커널–유저 영역의 책임을 명확히 분리**하여 설계했습니다.

<img width="800" height="435" alt="image" src="https://github.com/user-attachments/assets/4f13bc00-ecbc-43b9-ad7f-e34a3b6a3e04" />
<img src="docs/Demonstration video.gif" alt="Demonstration video.gif" width="800"/>

---

## 프로젝트 개요

본 프로젝트는 다음과 같은 학습 목표를 기반으로 진행되었습니다.

- Linux Kernel Module 구조 이해 및 직접 구현
- Character Device Driver 기반 시스템 콜 인터페이스 설계
- 인터럽트 기반 입력 처리 및 데이터 동기화
- 커널 영역과 유저 영역의 책임 분리 설계 경험

---

## 시스템 구성

### Hardware
- DS1302 RTC (실시간 시계)
- DHT11 (온도 / 습도 센서)
- EC11 Rotary Encoder (입력 장치)
- OLED Display (I2C)

### Software
- Embedded Linux
- Linux Kernel Module
- Character Device Driver
- User Application (`read()` 기반)

---

## 기술 요소
**Kernel / OS**

![Kernel](https://img.shields.io/badge/Kernel-Linux%20Kernel%20Module-0A66C2?style=flat)
![Linux](https://img.shields.io/badge/OS-Embedded%20Linux-333333?style=flat&logo=linux&logoColor=white)

**Language / Interface**

![C](https://img.shields.io/badge/Language-C-00599C?style=flat&logo=c&logoColor=white)
![CharDevice](https://img.shields.io/badge/Interface-Character%20Device%20Driver-4CAF50?style=flat)

**Hardware / Communication**

![I2C](https://img.shields.io/badge/Comm-I2C-6A1B9A?style=flat)
![1Wire](https://img.shields.io/badge/Comm-1--Wire-00897B?style=flat)
![GPIO](https://img.shields.io/badge/Comm-GPIO-455A64?style=flat)
![IRQ](https://img.shields.io/badge/Feature-Interrupt%20(IRQ)-D84315?style=flat)

---

## 시스템 아키텍처

| 계층 | 구성 요소 | 역할 |
|-----|----------|------|
| Hardware | RTC / DHT11 / Rotary Encoder | 시간·환경·입력 데이터 생성 |
| Kernel Space | Character Device Driver | 센서 제어, IRQ 처리, 데이터 수집 |
| Kernel Interface | read() / copy_to_user() | 커널 → 유저 데이터 전달 |
| User Space | User Application | 데이터 파싱, 시간 계산, 출력 처리 |
| Output | OLED Display | 시계 및 환경 정보 시각화 |

- 커널 드라이버는 **Raw 데이터 수집 및 전달**만 담당
- 유저 애플리케이션에서 데이터 해석 및 시각 처리 수행

---

## 설계 핵심 포인트

| 설계 항목 | 설명 |
|----------|------|
| 단일 시스템 콜 | 여러 센서 데이터를 하나의 `read()` 인터페이스로 전달 |
| 역할 분리 | 커널: 제어·수집 / 유저: 계산·표현 |
| 인터럽트 처리 | 로터리 엔코더 입력을 IRQ 기반 FSM으로 관리 |
| 안정성 | Race Condition 및 타이밍 이슈 대응 구조 설계 |

---

## 커널 드라이버 설계

### FSM 기반 동작 흐름

로터리 엔코더 입력과 센서 갱신 흐름을 명확히 제어하기 위해
FSM 기반으로 드라이버 동작을 구성했습니다.

| State | 설명 |
|------|------|
| S0 | IDLE |
| S1 | GPIO IRQ 발생 |
| S2 | IRQ Handler 실행 |
| S3 | 센서 데이터 갱신 |
| S4 | User `read()` 요청 |
| S5 | `copy_to_user()` 수행 |

---

### 데이터 전달 구조

여러 센서 데이터를 개별 시스템 콜로 나누지 않고,  
하나의 커널 버퍼에 통합하여 전달하도록 설계했습니다.

| 필드 | 설명 |
|------|------|
| RTC_TIME | RTC에서 읽은 현재 시각 정보 |
| TEMP | 온도 센서 값 |
| HUMID | 습도 센서 값 |
| ENC_CNT | 로터리 엔코더 누적 회전 값 |
| MODE | 현재 동작 모드 상태 |

- 커널 드라이버는 위 데이터를 하나의 버퍼로 구성하여 `read()`를 통해 전달
- 유저 애플리케이션에서 `sscanf()` 기반으로 필드별 파싱 수행

---

## 주요 기술 요소

### I2C (OLED)
- 문자 및 그래픽 출력
- 부분 초기화를 통한 잔상 제거

### 1-Wire (DHT11)
- μs 단위 타이밍 제어
- 데이터 수신 안정화를 위한 타이밍 보정

### 3-Wire Serial (DS1302 RTC)
- Half-Duplex 통신
- BCD → Decimal 변환 처리

### GPIO & Interrupt (Rotary Encoder)
- Quadrature Signal 기반 회전 방향 판별
- 소프트웨어 디바운싱 적용

---

## 트러블슈팅

### 1) 센서 통신 타이밍 불안정
- **원인**:
  - 멀티태스킹 환경에서 μs 단위 타이밍 왜곡
  - 멀티태스킹 환경에서 스케줄링으로 인해 정확한 타이밍 보장이 어려웠음
- **해결**: 데이터 수집 구간에서 인터럽트 제한
- **결과**: 센서 데이터 수신 안정화

### 2) 데이터 포맷 불일치 (BCD / Decimal)
- **원인**: RTC 데이터 포맷 차이
- **해결**: 비트 연산 기반 실시간 변환
- **결과**: 시스템 간 데이터 정합성 확보

### 3) Race Condition 발생
- **원인**: 사용자 입력과 데이터 읽기 동시 발생
- **해결**: 상태 플래그 기반 상호 배제 로직 설계
- **결과**: 시스템 멈춤 현상 제거

---

## 담당 역할

- Linux 커널 캐릭터 디바이스 드라이버 설계 및 구현
- 인터럽트 기반 로터리 엔코더 입력 처리
- RTC / DHT11 / OLED 통합 제어
- 커널–유저 영역 데이터 전달 구조 설계
- 시스템 안정성 및 동기화 문제 해결

---

## 설계 관점에서의 인사이트

본 프로젝트를 통해  
**하드웨어 제어 → 커널 드라이버 → 유저 애플리케이션**으로 이어지는  
임베디드 리눅스 전체 스택을 직접 설계하고 구현했습니다.

- 커널은 계산이 아닌 **안정적인 데이터 전달**에 집중해야 함
- 인터럽트 및 타이밍 제어가 시스템 신뢰성에 큰 영향을 미침
- 역할 분리 설계가 유지보수성과 확장성을 크게 향상시킴
- I2C / 1-Wire / GPIO 등 **통신 방식별 데이터 흐름 특성과 제약**을 이해하고,
  각 인터페이스에 맞는 처리 구조를 설계하는 경험을 얻음

---
