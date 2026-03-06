//https://youtu.be/9fPK-ZhgJAQ
/*유의사항....?
파일의 길이를 35글자로 제한, 폴더는 33글자([] 포함), 최상위 or 폴더 경로 안에 최대 10개의 이름이 존재하게끔(폴더 안에는 [<-]있음을 유의) --> 늘리고 싶으면 늘려도 된다(define 부분). 메모리 문제를 관리하기 위해 큰 숫자를 집어넣지 않았다,
현재 저의 코드 기준으로는 14개 * 35글자가 MAX인 것으로 보입니다(15개로 할 시 폴더 내부 들어갈 때 파일이 나타나지 않는다).
SdFat by Bill Greiman 라이브러리 사용, 반시계로 돌려야 다음 파일로 진행(peripheral board에 있는 rotary encoder 특성),
Serial.print는 주석 처리(나중에 문제 있다면 주석 해제 후 확인용도로 사용), ISR에서 많은 일을 처리하지 않도록 코드 작성(다른 인터럽트가 발생하는 것에 영향을 줄 수도 있고, loop()가 실행을 오랫동안 기다리는 등의 이유로 고속 작업을 하도록 함)
16BIT의 경우 그냥 그대로 음원 파일을 가져오면 음질이 상당히 깨짐 --> 강의자료에 명시되어 있듯이 소리를 줄여 export 함, 또한 16bit 음원 실행의 경우 큰 음이 발생할 때 LCD가 살짝 깜빡거리는데 전류를 많이 사용해서 그런 현상이 발생한다고 생각한다.
stereo 16bit 44.1khz의 경우 약간 느려지는데 저는 아두이노의 성능 한계로 인해 그렇다고 생각합니다
*/

/* 활용한 자료들
rotary encoder 회전 시 external interrupt3 사용(9주차 강의자료 활용, test 결과로 cw로 돌리고 falling edge로 설정 시 --> phase b : high),
Rotary encoder push 시 external interrupt1 사용*, PWM 신호 생성시 T/C를 활용하며 PWM 신호 생성 주기는 T/C INT1을 활용해 생성한다(7, 9주차 강의자료 활용),
정보를 받아올 때는 10주차 강의자료를 활용해 각 위치에서 가져옴, mono는 둘 다 동일하지만 stereo는 LR 순으로 data가 있다(16bit의 경우 LLRR), 
파일 형식 8.3외에 다른 것도 사용가능(SdFat 라이브러리 사용), SD card init + 폴더 파일 이름 가져오기(SdFat 예제 사용)
5주차 과제 활용 --> LED 1~7개 -- 1~7초 시간 이동 기능 구현, 7등분, LCD 출력(8주차 과제 활용, Register 사용),
스위치1, 2 눌르면 0으로(FALLING EDGE)
*/

/*구현해야할 기능들
1. 맨 처음에는 폴더, 파일 모두 있어도 되며, 폴더 안 진입 시 파일만 존재
2. SdFat library를 활용해 8.3 format이 아닌 일반적인 형식 지원, 사용 방법 숙지
3. LCD 첫번째 줄 : 곡명(최대 16자, 파일은 그대로, 폴더는 [] 추가), 두번째 줄: 현재 상태, register 사용
4. int3을 활용해 rotary encoder 회전하여 위치 변경(음악이 멈춰있을 때만)
5. int1을 활용해 rotary encoder push 시 안으로 들어가거나(폴더) or play, stop(파일) or 상위 디렉토리 위치로([<-])
6. 특정 곡 종료되면 다음 곡 재생
7. mo/st 16/8 bit 22.05/44.1kHz(t/c1 이용) 모두 사용 가능하게
8. 8bit pwm(t/c2, 4)
9. vr1 + sw1,2 + led 개수를 이용해 앞 뒤로 이동
*/

//23개의 함수 정의 및 사용
void LCD_command(unsigned char command);
void LCD_data(unsigned char data);
void LCD_init(void);
void LCD_string(unsigned char command, const char *string);
void SD_init();
void update_name(const char *path);
void Ext_interrupt3_init();
void Ext_interrupt1_init();
void Ext_interrupt0_init(); 
void Ext_interrupt5_init(); 
void timer1_init();
void timer2_init(); 
void timer4_init(); 
void get_information(const char *target_file); 
void LCD_play2stop(); 
void LCD_stop2play();
void push_file(const char *path);
void push_button(int name_index); 
void playing_update(float j);
void read_buf_2_make_pwm();
void fill_buffer(const char *target_file);
void next_play();
void go_back_offset(int seconds);
  
#include <SdFat.h> //SD card 관련 제어 함수
#include <stdint.h> //(u)intX_t 형 type 사용을 위해, 몇 bit인지 명시
#include <string.h> //string(문자열)관련 함수들을 사용하기 위해
#define LCD_DATABUS PORTF //결선 정보에 따라서(D0~7--A0~A7/PFn)
#define LCD_CONTROL PORTB //결선 정보에 따라서(E,RS--11,12/PB5,6)
#define MAX_FILE_FOLDER_NUMBER 10 //최상위 or 폴더 경로 안데 최대 10개의 이름이 존재하게끔 설정(폴더 안에는 [<-]있음을 유의), 이 경우 늘리고 싶으면 늘려도 된다(메모리 고려하여),현재는 메모리 문제를 잘 관리해야 하므로 제한함
#define MAX_NAME_LENGTH 35 //파일의 길이를 35글자로 제한, 폴더는 33글자([] 포함), 보통 노래 길이 33글자가 안 넘는다고 생각하여 결정, 혹시 파일 이름이 길어질 시 변경, 그러나 메모리 확인하여 변경해야함(버퍼가 6000바이트 사용)
#define PhaseA 18 //결선 정보에 따라서(18 -- PH_A)
#define PhaseB 19 //결선 정보에 따라서(19 -- PH_B)
#define VOL_MAX 1023 //5V --> 1023, A9는 10-bit 정보를 받는다.(1024-1 = 1023)

/*밑에 내용은 SD card의 format 형식을 type에 따라 분류한 것, type 3(8.3, 더 긴 것 모두 사용 가능) 사용: 모든 형식을 사용할 수 있기 때문(8.3 형식 외에)
SD_FAT_TYPE = 0 for SdFat/File as defined in SdFatConfig.h,
1 for FAT16/FAT32, 2 for exFAT(여러줄), 3 for FAT16/FAT32 and exFAT(모두) --> 저는 그래서 3번 type을 사용하겠습니다.
#if SD_FAT_TYPE == 0
SdFat sd;
File file;
File root;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
File32 file;
File32 root;
#elif SD_FAT_TYPE == 2
SdExFat sd;
ExFile file;
ExFile root;
#elif SD_FAT_TYPE == 3, 이것이 제가 사용할 type입니다.
SdFs sd;
FsFile file;
FsFile root;
#endif*/
SdFs sd; //SD card 전체를 관리하는 객체, 파일을 읽고 쓰려면 sd.begin()이 성공해야한다
FsFile file; //특정 폴더 or 파일 가리키는 핸들, 실제 파일을 열어 데이터를 읽을 때 사용, 즉, 실제 play할 때 사용
FsFile root; //SD card의 특정 위치를 가리키는 핸들, /(가장 최상위 위치)나 폴더 경로를 열어 내부 항목 탐색 시 사용, 즉, 이름 받아올 때 사용

char full_name[MAX_FILE_FOLDER_NUMBER][MAX_NAME_LENGTH]; //최대 개수와 최대 길이의 특성을 지키는 이름 배열, 위 define 부분에서 변경 가능
char current_path[2 * MAX_NAME_LENGTH]; //가장 경로의 길이가 클 때는 /폴더/파일: 2 + 33 + 35 = 70, []이것이 //개수랑 동일
char play_path[2*MAX_NAME_LENGTH]; //연주 시 필요로 하는 경로
char data[7] = {0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F}; //1~7번 LED까지 사용할 것이므로 0x7F까지
bool inside_folder = false; //폴더 안 밖 각 경우에 대해 수행해야 할 업무가 다르다
bool first_play = true; //중간에 멈췄는지 아닌지를 확인하기 위해, true: 첫 연주, false: 멈췄다가 다시 연주
bool go = false; //앞으로 가기 일 때 true
bool back = false; //뒤로 가기 일 때 true
int maxsize = 0; //현재 경로에서 몇개의 파일 or 폴더를 가지고 있는지, 처음에는 안에 몇개가 있는지 모르므로 0개
int cnt_prev = 0; //[<-]를 클릭하여 폴더 안에서 다시 나올 때 들어왔던 폴더명을 다시 출력해야하므로 이전 위치를 기억해야한다
int level = 1; //led on 개수에 따라서 앞 뒤로 이동하기 위해, 디폴트는 1개
volatile int cnt = 0; //interrupt3에서 사용하는 변수, 최적화를 막기 위해 volatile 사용
volatile bool int3_worked = false; //이 변수로 interrupt3가 작동했는지 확인
volatile bool int1_worked = false; //interrupt1이 작동했는지 알기 위해 사용하는 변수
volatile bool int0_worked = false; //interrupt0이 작동했는지 알기 위해 사용하는 변수
volatile bool int5_worked = false; //interrupt5이 작동했는지 알기 위해 사용하는 변수
bool stop_song = true; //맨 처음은 stop이므로 true
bool buffer_need_filling[2] = {true, true}; //처음 시작에는 두 버퍼를 채워줘야 하므로 --> true
bool final = false; //버퍼에 더 이상 채울 데이터가 없으면 마지막 버퍼를 읽으라는 의미의 변수, 연주가 다 됐다는 의미
uint16_t num_channels; //다음 변수들은 seek read 후 얻은 바이트를 10진수의 결과로 바꾸어 주기 위해 사용한다. 2byte = 16 bit, 4byte = 32bit, 각 앞글자만 가져와서 표시
uint32_t sample_rate;
uint16_t bits_per_sample;
uint32_t sub_chunk_2_size;
uint32_t play_offset = 44; //data를 읽어올 위치는 44부터 시작이며, 중간에 stop되고 다시 play할 때는 멈춘 위치에서부터 다시 연주해야 한다(44에서 계속해서 위치가 업데이트), uint32_t : offset이 엄청 클 수도 있으므로
uint16_t sample_number = 0; //현재 버퍼에서 몇 번쨰 샘플을 사용했는지 알기 위해서, 끝에 도달하면 clear하고 다음 내용을 채운다, 3000이 최대값 -> 16bit 사용
uint32_t sample_total_number = 0; //총 몇개의 버퍼를 읽었는지, 퍼센트지 표시할 때 사용, data 받는 크기가 32bit이므로
int buf_index = 0; //buffer index는 0 or 1 
int buf_read[2]; //첫번째, 두번째 버퍼가 얼마큼 읽었는지 알기 위해
unsigned char buf[2][3000]; //명시된 크기, 프로젝트 조건
unsigned long percent_last_time = 0; //loop문에서 특정 주기동안 나오게 하기 위해
unsigned long vr1_change_check_time = 0; //loop문에서 특정 주기동안 anal1 값을 받아 led에 표시하기 위해
float j = 0.0; //몇개의 data가 사용됐는지 알기 위해서

//LCD 출력 시 사용하는 함수들
void LCD_command(unsigned char command){ //instruction register
  LCD_CONTROL = 0x00; //RS, E = 0
  LCD_DATABUS = command; //명령 내용을 받아 databus에 저장(총 9가지 기능)
  LCD_CONTROL = 0x20; //E = 1(write enable)
  asm volatile("PUSH R0"); //무의미한 행동, 단순히 시간을 사용하기 위해(약 250ns)
  asm volatile("POP R0");
  LCD_CONTROL = 0x00; //E = 0
  delayMicroseconds(50);
}
void LCD_data(unsigned char data){ //data register
  LCD_CONTROL = 0x40; //RS = 1, E = 0
  LCD_DATABUS = data; //data 내용을 받아 write하기 위해 databus에 저장
  LCD_CONTROL = 0x60; //E = 1(write enable)
  asm volatile("PUSH R0"); //무의미한 행동, 단순히 시간을 사용하기 위해(약 250ns)
  asm volatile("POP R0");
  LCD_CONTROL = 0x40; //E = 0
  delayMicroseconds(50);
}
void LCD_init(void){ //LCD 출력 등에 대해서 미리 설정해 놓을 것
  LCD_command(0x38); //6번 function set --> DL = 1: 8bit, N = 1: 2line, F = 0: 5X8 dots
  LCD_command(0x0C); //4번 Display ON/OFF control --> D = 1: display on, C, B = 0: cursor, blink x
  LCD_command(0x06); //3번 entry mode set --> I/D = 1, S = 0: cursor가 오른쪽으로 움직임, content shift x
  LCD_command(0x01); //1번 clear display
  delay(2);
}
void LCD_string(unsigned char command, const char *string){ //문자열인 경우 사용(위는 문자다)
  LCD_command(command); //0x80: 00에서 시작(첫 번째 줄 처음), 0xC0: 40에서 시작(두 번째 줄 처음) --> 어디서부터 출력 할 것인지 명령을 넘겨준다
  while(*string != '\0'){ //문자열 모두 다 출력 시 끝낸다
    LCD_data(*string); //한글자 한글자씩 계속해서 출력
    string++; //다음 글자로 이동을 위해
  }
}
//SD 초기화 함수
void SD_init(){ //SD card 초기화
  //Serial.println("initializing SD card...");
  if (!sd.begin(53)){ //SD card 초기화, sd.begin 성공하지 못하면 0 -- ! -- 1로 되며 다음 문장 출력
    //Serial.println("initializing failed!");
    return;
  }
  //Serial.println("initializing done.");
}
//현재 경로를 받아 그 위치에서 탐색을 통해 파일 or 폴더 이름을 저장하는 함수
void update_name(const char *path){ //const: path 문자열을 변경하지 않기 위해, 함수에 전달할 때 문자 배열 전체가 아니라 첫 글자의 주소만 전달 --> 포인터 사용
  memset(full_name, 0, sizeof(full_name)); //full_name 배열을 초기화, 경로가 바뀐다면 그 위치에서의 이름들이 달라지므로
  maxsize = 0; //
  int i = 0;
  if(root.isOpen()) root.close(); //이전 root 열려 있으면 닫기
  if(!root.open(path, O_RDONLY)){ //open 여부 확인
    //Serial.println("root open failed!");
    return;
  }
  //경로가 열렸음을 시리얼 모니터로 확인하고 싶으면 확인
  //Serial.print(path);
  //Serial.println(" :path is open!");
  strncpy(current_path, path, sizeof(current_path)); //현재 경로를 저장해준다(나중에 파일 열 때 사용)
  if(file.isOpen()) file.close(); //file이 열려있으면 닫기
  char n[MAX_NAME_LENGTH]; //미리 정해준대로 제한된 길이만큼 받는다
  while(file.openNext(&root, O_RDONLY)){ //openNext: 현재 위치에서 폴더 or 파일 탐색, 더 이상 탐색할 것 없으면 빠져나온다
    file.getName(n, sizeof(n)); //현재 탐색한 파일 or 폴더 이름을 get하여 n에 저장
    if(file.isDir()){ //폴더라면
      snprintf(full_name[i], sizeof(full_name[i]), "[%s]", n); //[]표시 추가
    } 
    else snprintf(full_name[i], sizeof(full_name[i]), "%s", n); //파일 이라면 이름만 사용
    if(i > 9) { //최대 10개이므로 index는 9까지가 최대이다.
      //Serial.println("현재 위치에서의 개수 10개 초과!");
      file.close(); //이번 if문이 실행됐다면 더이상 file을 사용하지 않는다
      break; //최대 10개로 설정, unsigned char buf를 사용하려면 name의 크기가 커서는 안되며 적당히 한 위치에서 10개정도 max라고 하면 충분하다고 생각합니다.
    }
    i++; //이름을 계속해서 저장하기 위해
    maxsize++; //현재 위치에서 몇개의 파일 or 폴더가 있는지 알기 위해서 --> 이를 활용해 스크롤의 제한을 둔다
  }
  file.close(); //openNext가 끝났다면 더 이상 file을 사용하지 않으므로 닫아준다
  if((strcmp(path, "/"))){ //문자열의 내용 비교시 strcmp함수 사용(비교시 같으면 0), 경로가 폴더라면 맨 마지막에 [<-] 표시를 해줘야 한다
    snprintf(full_name[maxsize], sizeof(full_name[maxsize]), "%s", "[<-]"); //index 0~maxsize-1까지 이름이 저장됨, maxsize인 index에 [<-] 문자열을 넣어준다
    inside_folder = true; //폴더 안으로 들어왔으므로 true로 변경
    maxsize++; //하나 더 들어갔으므로
  }
  else inside_folder = false; //최상위 디렉토리에 위치해 있으므로
}
//led 개수 만큼 앞 뒤로 가기 int0, 5 이용
void Ext_interrupt0_init(){
  //INT0은 EICRA사용, falling edge 사용(스위치를 누르면 low가 된다)
  EICRA &= ~_BV(ISC00);
  EICRA |= _BV(ISC01);
  EIFR |= _BV(INTF0); //flag clear(1을 사용해 clear한다)
  EIMSK |= _BV(INT0); //external interrupt0을 enable한다, 맨 처음에는 stop이므로 enable 시켜야 한다.
}
ISR(INT0_vect){
  int0_worked = true;
}
void Ext_interrupt5_init(){
  //INT0은 EICRB사용, falling edge 사용(스위치를 누르면 low가 된다)
  EICRB &= ~_BV(ISC50);
  EICRB |= _BV(ISC51);
  EIFR |= _BV(INTF5); //flag clear(1을 사용해 clear한다)
  EIMSK |= _BV(INT5); //external interrupt5을 enable한다, 맨 처음에는 stop이므로 enable 시켜야 한다.
}
ISR(INT5_vect){
  int5_worked = true;
}
//rotary encoder를 회전시킬 때 interrupt3이 발생하고 그에 관한 행동 및 레지스터 설정
void Ext_interrupt3_init(){
  //INT3은 EICRA사용, 저는 falling edge 사용하겠습니다
  EICRA &= ~_BV(ISC30);
  EICRA |= _BV(ISC31);
  EIFR |= _BV(INTF3); //flag clear(1을 사용해 clear한다)
  EIMSK |= _BV(INT3); //external interrupt3을 enable한다, 맨 처음에는 stop이므로 enable 시켜야 한다.
}
ISR(INT3_vect){ //interrupt에서는 너무 많은 일을 수행하지 않도록 하기 위해 간단한 조작만
  int3_worked = true; //interrupt3가 실행됐으므로
  if(digitalRead(19)) cnt--; //falling edge시 값을 확인하여 high:cw, low:ccw --> 즉, ccw로 돌려야지 다음 파일 or 폴더를 출력
  else cnt++; //cw로 움직이면 이전 폴더 or 파일이 LCD에 출력
  if(inside_folder == true){ //폴더 안이면
    if(cnt < 0) cnt = 0; //혹시 반시계로 계속 돌려 음수가 나오더라도 0으로 설정, 첫 파일 이름에서 거꾸로 가는 것을 사용하지 않음
    if(cnt > maxsize - 1) cnt = maxsize-1; //혹시 반시계로 계속 돌려 현재 개수보다 높게 될 때 max값으로 설정(index는 0부터, maxsize index에 [<-] 문자열 추가 했으므로), 마지막 이름에서 처음으로 가는 것을 사용하지 않음
  }
  else{ //폴더 밖이면
    if(cnt < 0) cnt = 0; //혹시 반시계로 계속 돌려 음수가 나오더라도 0으로 설정, 첫 파일 이름에서 거꾸로 가는 것을 사용하지 않음
    if(cnt > maxsize - 1) cnt = maxsize - 1; //혹시 반시계로 계속 돌려 현재 개수보다 높게 될 때 max값 - 1으로 설정(index는 0부터!), 마지막 이름에서 처음으로 가는 것을 사용하지 않음
  }
}
//rotary encoder를 push할 때 interrupt1이 발생하고, 이와 관련한 설정 및 진행 내용
void Ext_interrupt1_init(){
  //INT1은 EICRA사용, falling edge 사용(push시 low로 변경)
  EICRA &= ~_BV(ISC10);
  EICRA |= _BV(ISC11);
  EIFR |= _BV(INTF1); //flag clear(1을 사용해 clear한다)
  EIMSK |= _BV(INT1); //external interrupt1을 enable한다
}
ISR(INT1_vect){ //interrupt에서는 너무 많은 일을 수행하지 않도록 하기 위해 true 조작만 하게 함, button_push 함수는 loop문에서 수행, 
  int1_worked  = true; //interrupt1 실행됐다고 알려주는 변수, 정지해있을때 사용
}
void timer1_init(){ //9주차 강의자료 활용
  //CTC mode
  TCCR1B &= ~_BV(WGM13);
  TCCR1B |= _BV(WGM12);
  TCCR1A &= ~_BV(WGM11);
  TCCR1A &= ~_BV(WGM10);
  //prescaler = 1
  TCCR1B &= ~_BV(CS12);
  TCCR1B &= ~_BV(CS11);
  TCCR1B |= _BV(CS10);
  //16MHz / samplerate - 1 = OCRNX(prescaler 1), 44.1: 363 22.05: 726
  if(sample_rate == 44100){
    OCR1A = 363;
  }
  else if(sample_rate == 22050){
    OCR1A = 724;
  }
  TCNT1 = 0x0000;
  TIFR1 |= _BV(OCF1A); //clear 할때 1을 써서 한다.
  TIMSK1 |= _BV(OCIE1A); //interrupt를 enable
}
ISR(TIMER1_COMPA_vect){
  if(stop_song){ //노래가 멈추면 PWM도 멈춘다
    TIMSK1 &= ~_BV(OCIE1A); //interrupt를 비활성화(노래가 멈추면 굳이 인터럽트가 발생할 필요가 없다)
    OCR2A = 0;
    OCR2B = 0;
    OCR4A = 0;
    OCR4B = 0;
    return;
  }
  else read_buf_2_make_pwm(); //노래가 안멈춰있다면 버퍼를 계속 읽는다
}
//timer/counter1의 interrupt 주기보다 timeer/counter2를 활용한 pwm 생성 주기가 더 짧아야 가능(pwm duty를 이용해 음악 소리를 만든다!)
void timer2_init(){
  //fast pwm 8 bit mode, timer/counter 2(8bit)
  TCCR2B &= ~_BV(WGM22);
  TCCR2A |= _BV(WGM21);
  TCCR2A |= _BV(WGM20);
  //clear on compare match(OC2A, OC2B)
  TCCR2A |= _BV(COM2A1);
  TCCR2A &= ~_BV(COM2A0);
  TCCR2A |= _BV(COM2B1);
  TCCR2A &= ~_BV(COM2B0);
  //prescaler = 1 --> top값이 0~255인데 어떤 값이든지 주파수는 파일의 주파수보다 크다
  TCCR2B &= ~_BV(CS22);
  TCCR2B &= ~_BV(CS21);
  TCCR2B |= _BV(CS20);
}
void timer4_init(){
    //fast pwm 8 bit mode, timer/counter 4
  TCCR4B &= ~_BV(WGM43);
  TCCR4B |= _BV(WGM42);
  TCCR4A &= ~_BV(WGM41);
  TCCR4A |= _BV(WGM40);
  //clear on compare match(OC4A, OC4B)
  TCCR4A |= _BV(COM4A1);
  TCCR4A &= ~_BV(COM4A0);
  TCCR4A |= _BV(COM4B1);
  TCCR4A &= ~_BV(COM4B0);
  //prescaler = 1 --> top값이 0~255인데 어떤 값이든지 주파수는 파일의 주파수보다 크다
  TCCR4B &= ~_BV(CS42);
  TCCR4B &= ~_BV(CS41);
  TCCR4B |= _BV(CS40);
}
void go_back_offset(int seconds){
  if(go == true){
    if(sub_chunk_2_size - sample_total_number < seconds * sample_rate * (bits_per_sample/8) * num_channels){ //앞으로 더 못갈 때는 그대로 진행
      //Serial.println("더이상 앞으로 못가서 그대로 진행");
      return;
    }
    OCR2A = 0; //중간에 팍 튀는 소리를 없애기 위해서 아예 잠깐 0으로 하고 데이터 받고 인터럽트 실행시 다시 계산하여 pwm만들어서 음악생성
    OCR2B = 0;
    OCR4A = 0;
    OCR4B = 0;
    memset(buf[0], 0, sizeof(buf[0]));
    memset(buf[1], 0, sizeof(buf[1]));
    if(file.isOpen()) file.close(); //이전에 열었던 file이 있으면 닫고 다시 open
    if(!file.open(play_path, O_RDONLY)) { //현재 노래하고 있는 파일 열기
      //Serial.println("file open failed for play");
      return;
    }
    play_offset = play_offset + seconds * sample_rate * (bits_per_sample/8) * num_channels; //play offset update
    file.seek(play_offset); //다시 버퍼 채워주고, 채웠을 때 초기화해야하는 변수들 초기화
    buf_read[0] = file.read(buf[0],3000);
    buf_read[1] = file.read(buf[1],3000);
    sample_number = 0;
    buf_index = 0;
    sample_total_number = play_offset; //총 읽은 개수를 play_offset으로 맞춰주는 것이 맞다(그 위치만큼 앞으로 점프한 것이므로)
    go = false;
    return;
  }
  if(back == true){
    if(sample_total_number < seconds * sample_rate * (bits_per_sample/8) * num_channels){ //뒤로 더 못갈 때는 그대로 진행
      //Serial.println("더이상 뒤로 못가서 그대로 진행");
      return;
    }
    OCR2A = 0; //중간에 팍 튀는 소리를 없애기 위해서 아예 잠깐 0으로 하고 데이터 받고 인터럽트 실행시 다시 계산하여 pwm만들어서 음악생성
    OCR2B = 0;
    OCR4A = 0;
    OCR4B = 0;
    memset(buf[0], 0, sizeof(buf[0]));
    memset(buf[1], 0, sizeof(buf[1]));
    if(file.isOpen()) file.close(); //이전에 열었던 file이 있으면 닫고 다시 open
    if(!file.open(play_path, O_RDONLY)) { //현재 노래하고 있는 파일 열기
      //Serial.println("file open failed for play");
      return;
    }
    play_offset = play_offset - seconds * sample_rate * (bits_per_sample/8) * num_channels; //play offset update
    file.seek(play_offset); //다시 버퍼 채워주고, 채웠을 때 초기화해야하는 변수들 초기화
    buf_read[0] = file.read(buf[0],3000);
    buf_read[1] = file.read(buf[1],3000);
    sample_number = 0;
    buf_index = 0;
    sample_total_number = play_offset; //총 읽은 개수를 play_offset으로 맞춰주는 것이 맞다(그 위치만큼 뒤로 점프한 것이므로)
    back = false;
    return;
  }
}
//SD card로부터 wave file의 정보들을 얻는 함수
void get_information(const char *target_file){ //문자열을 포인터로 받기, 8 or 16-bit, 44.1 or 22.05kHz, mono or stereo 구현 --> SdFat의 함수 사용 --> 정보 읽기, seek: wav file format에서 원하는 위치의 바이트로 이동, read:그 위치부터 정해진 크기까지 읽어오는 함수
  //밑의 변수들은 앞글자만 가져왔습니다(full name은 전역변수로 선언)
  byte n_c[2]; //정보의 크기가 2byte이다.
  byte s_r[4]; //정보의 크기가 4byte이다.
  byte b_p_s[2]; //정보의 크기가 2byte이다.
  byte s_c_2_s[4]; //정보의 크기가 2byte이다.
  FsFile wavfile;
  if (wavfile.isOpen()) wavfile.close(); //파일 열려있으면 닫기
  if (!wavfile.open(target_file, O_RDONLY)) { //인자로 받은 파일명 open 여부 확인(seek, read를 위해), SdFs는 절대경로로 하는것이 더 안전
    //Serial.println("file open failed for info");
    return;
  }
  //Serial.print(target_file);
  //Serial.println(" :file is open");
  wavfile.seek(22); //NumChannels는 23번째부터 있다, 숫자이므로 little endian이다.
  wavfile.read(n_c, 2); //23~24번째까지 두개의 바이트가 NumChannels이다
  wavfile.seek(24); //samplerate는 25번째부터 있다, 숫자이므로 little endian이다.
  wavfile.read(s_r, 4); //25~28번째까지 네개의 바이트가 samplerate이다
  wavfile.seek(34); //bitspersample는 35번째부터 있다, 숫자이므로 little endian이다.
  wavfile.read(b_p_s, 2); //35~36번째까지 두개의 바이트가 bitspersample이다
  wavfile.seek(40); //subchunk2size는 41번째부터 있다, 숫자이므로 little endian이다.  
  wavfile.read(s_c_2_s, 4); //41~44까지 네개의 바이트가 subchunk2size이다.
  wavfile.close();
  num_channels = (uint16_t)n_c[0] | (uint16_t)n_c[1]<<8; //맨 오른쪽 바이트는 그대로, 그 다음부터는 8, 8*2, 8*3 .....만큼 shift해준다(만약 오른쪽에서 두번째 바이트라면 8번 shift해야한다.)
  sample_rate = (uint32_t)s_r[0] | (uint32_t)s_r[1]<<8 | (uint32_t)s_r[2]<<16 | (uint32_t)s_r[3]<<24; //맨 오른쪽 바이트는 그대로, 그 다음부터는 8, 8*2, 8*3 .....만큼 shift해준다(만약 오른쪽에서 두번째 바이트라면 8번 shift해야한다.)
  bits_per_sample = (uint16_t)b_p_s[0] | (uint16_t)b_p_s[1]<<8; //맨 오른쪽 바이트는 그대로, 그 다음부터는 8, 8*2, 8*3 .....만큼 shift해준다(만약 오른쪽에서 두번째 바이트라면 8번 shift해야한다.)
  sub_chunk_2_size = (uint32_t)s_c_2_s[0] | (uint32_t)s_c_2_s[1]<<8 | (uint32_t)s_c_2_s[2]<<16 | (uint32_t)s_c_2_s[3]<<24; //맨 오른쪽 바이트는 그대로, 그 다음부터는 8, 8*2, 8*3 .....만큼 shift해준다(만약 오른쪽에서 두번째 바이트라면 8번 shift해야한다.)
  //Serial.println(num_channels); //num_channels 등 필요한 정보에 대해 눈으로 직접 확인하고 싶으면 사용
  //Serial.println(sample_rate); //num_channels 등 필요한 정보에 대해 눈으로 직접 확인하고 싶으면 사용
  //Serial.println(bits_per_sample); //num_channels 등 필요한 정보에 대해 눈으로 직접 확인하고 싶으면 사용
  //Serial.println(sub_chunk_2_size); //num_channels 등 필요한 정보에 대해 눈으로 직접 확인하고 싶으면 사용
}
//play or stop 시 LCD 출력해주는 함수
void LCD_play2stop(){
  LCD_string(0xC0, "                ");
  LCD_string(0xC0, "STOPPED");
}
void LCD_stop2play(){
  LCD_string(0xC0, "                ");
  LCD_string(0xC0, "PLAYING");
}
//파일 push 시 해야할 일들
void push_file(const char *path){
  //Serial.print(path);
  //Serial.println(": 파일이 눌림");
  if(stop_song){ //노래가 멈춰있을 때 눌렀다면
    //Serial.println("stop2play");
    EIMSK &= ~_BV(INT3); //play이므로 비활성화 시켜야 한다.
    EIFR |= _BV(INTF0); //flag clear(1을 사용해 clear한다)
    EIMSK |= _BV(INT0);
    EIFR |= _BV(INTF5); //flag clear(1을 사용해 clear한다)
    EIMSK |= _BV(INT5);
    stop_song = false; //노래가 play될 것이다
    LCD_stop2play();
    if(first_play){ //첫 연주시에는 정보를 가져와야 한다, 그 다음부터는 그 정보를 저장해서 사용한다
      get_information(path);
      timer1_init(); //timer/counter1 interrupt 관련 레지스터 설정 초기화
      Ext_interrupt0_init(); //external int0 초기화
      Ext_interrupt5_init(); //external int5 초기화
      play_offset = 44; //첫 시작 읽는 곳은 44부터
      go = false;
      back = false;
    }
    fill_buffer(play_path); //T/C1 인터럽트 시작 전(노래 재생 전) 한번 채우고 인터럽트 다시 활성화, 항상 연주 전 버퍼를 먼저 채운다
    TCNT1 = 0x0000;
    TIFR1 |= _BV(OCF1A); //clear 할때 1을 써서 한다.
    TIMSK1 |= _BV(OCIE1A); //interrupt를 활성화
    return;
  }
  else{ //노래가 연주중에 눌렀다면
    //Serial.println("play2stop");
    EIFR |= _BV(INTF3); //flag clear(1을 사용해 clear한다)
    EIMSK |= _BV(INT3); //stop이므로 enable 시켜야 한다.
    EIMSK &= ~_BV(INT0);
    EIMSK &= ~_BV(INT5);
    LCD_play2stop();
    stop_song = true; //노래가 stop될 것이다
    memset(buf[0], 0, sizeof(buf[0])); //멈추면 버퍼 다 지워주고, 다시 시작시 새로운 offset에서 받아서 진행
    memset(buf[1], 0, sizeof(buf[1])); //멈추면 버퍼 다 지워주고, 다시 시작시 새로운 offset에서 받아서 진행
    buffer_need_filling[0] = true; //비웠으므로 다시 채워야 한다
    buffer_need_filling[1] = true;
    buf_read[0] = 0;
    buf_read[1] = 0;
    sample_number = 0; //버퍼 모두 초기화하고 다시 하므로
    buf_index = 0; //첫번째 버퍼부터 다시 사용할 것이므로
    return;
  }
}
//버튼 push 시 각 경우(폴더 안으로 들어가기 or play/stop)에 따라 수행할 일이 다르게 만들었습니다
void push_button(int name_index){
  char name_16_length[17]; //이름을 16글자로 줄여야(17번째는 null 사용)
  strncpy(name_16_length, full_name[name_index], sizeof(name_16_length)); //이름 복사
  name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
  if(inside_folder){ //폴더 안 경우 파일 or [<-] 만 있다 
    if(name_16_length[0] == '['){ //문자열 비교는 strcmp, 같으면 0출력, [<-] push 경우
    cnt = cnt_prev; //[<-]버튼에서 push하면 상위 폴더의 이름을 출력해야하므로, 이전에 저장해놓았던 index를 사용
    maxsize = 0; //상위로 올라가게 되므로 파일 개수가 달라지므로 초기화, 파일 개수는 update_name 함수에서 다시 센다 
    inside_folder = false; //폴더 밖으로 나가게 되므로
    strcpy(current_path, "/"); //최상위 위치로 가므로 현재 경로를 /로 업데이트
    update_name("/"); // 최상위 경로에서 있는 파일 or 폴더들 탐색
    strncpy(name_16_length, full_name[cnt], sizeof(name_16_length)); //폴더 이름을 다시 복사(LCD에 출력해야하므로)
    name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
    LCD_string(0x80, "                "); //이름 탐색 후 첫번째줄 초기화
    LCD_string(0x80, name_16_length); //이전에 기억했던 인덱스(폴더명)에 관한 이름을 출력
    return;
    }
    else{
      char file_path[2 * MAX_NAME_LENGTH]; //파일 선택 시 경로를 저장하기 위해
      snprintf(file_path, sizeof(file_path), "%s/%s", current_path, full_name[name_index]); // /폴더/파일 경로로 만들어줌
      snprintf(play_path, sizeof(play_path), "%s", file_path); // 연주할 때 사용할 경로 저장
      push_file(file_path);
      return;
    }
  }
  else{
    if(name_16_length[0] == '['){ //최상위에서 [폴더] push 경우
      int f_l = strlen(name_16_length);
      int full_len = strlen(full_name[name_index]);
      memmove(name_16_length, name_16_length + 1, f_l - 2); //폴더명을 복사하여 name_16_length에 저장 -- > 폴더명, +1: 폴더명 시작부터, len - 2 : 폴더명 문자열 개수까지
      memmove(full_name[name_index], full_name[name_index] + 1, full_len - 2); //폴더명을 복사하여 name_16_length에 저장 -- > 폴더명, +1: 폴더명 시작부터, len - 2 : 폴더명 문자열 개수까지
      name_16_length[f_l - 2] = '\0'; //마지막에 null 넣어주기
      full_name[name_index][full_len - 2] = '\0'; //마지막에 null 넣어주기
      cnt_prev = cnt; // [<-]에서 push하면 이전 위치로 돌아가야 하므로 현재 위치를 cnt_prev라는 변수에 저장
      cnt = 0; //폴더라면 해당 폴더 안으로 들어가게 되며 그렇다면 cnt를 초기화 하여 폴더 안에 있는 파일 중 처음 파일 이름을 출력하게 해야한다.
      maxsize = 0; //폴더 안으로 들어가게 되면 파일 개수가 달라지므로 초기화
      char folder_path[MAX_NAME_LENGTH - 2 + 1]; // /폴더 형태의 경로를 사용하기 위해, 1 + 28([] 제외 했으므로)
      snprintf(folder_path, sizeof(folder_path), "/%s", full_name[name_index]); //현재 제일 상위 위치에서 폴더 push 경우 /폴더 형태의 경로
      update_name(folder_path);
      strncpy(name_16_length, full_name[0], sizeof(name_16_length)); //파일 이름을 다시 복사(LCD에 출력해야하므로)
      name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
      LCD_string(0x80, "                "); //이름 탐색 후 첫번째줄 초기화
      LCD_string(0x80, name_16_length); //폴더 안으로 들어간 후 거기에서의 이름 중 첫번쨰 출력
      return;
    } 
    else{
      char file_path[2 * MAX_NAME_LENGTH]; //파일 선택 시 경로를 저장하기 위해
      snprintf(file_path, sizeof(file_path), "/%s", full_name[name_index]); // /파일 경로로 만들어줌
      snprintf(play_path, sizeof(play_path), "%s", file_path); // 연주할 때 사용할 경로 저장
      push_file(file_path);
      return;
    }
  }
}
void next_play(){ //다음 연주시 버퍼 등의 설정 초기화를 해줘야 정보 가져온다
  memset(buf[0], 0, sizeof(buf[0]));
  memset(buf[1], 0, sizeof(buf[1]));
  buffer_need_filling[0] = true;
  buffer_need_filling[1] = true;
  buf_read[0] = 0;
  buf_read[1] = 0;
  sample_number = 0;
  sample_total_number = 0; 
  buf_index = 0;
  final = false; //곡이 끝났다
  int pass = 0; //다 통과하면 stopped 상태로 바꾸려고 사용
  if(!strcmp(current_path,"/")){ //최상위 경로에서 파일 재생 시
    for(int i = 1; cnt + i < maxsize;i++){ //현재 위치부터 maxsize-1 index 위치까지 돌려보는데 중간에 파일이 있으면 그 파일을 연주 없으면 stopped로 상태 변경
      if(full_name[cnt + i][0] == '['){
        pass++;
        continue; //폴더면 pass
      }
      else{
        cnt = cnt + i; //index update
        char name_16_length[17]; //이름을 16글자로 줄여야(17번째는 null 사용)
        strncpy(name_16_length, full_name[cnt], sizeof(name_16_length)); //폴더 이름을 다시 복사(LCD에 출력해야하므로)
        name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
        LCD_string(0x80, "                "); //이름 탐색 후 첫번째줄 초기화
        LCD_string(0x80, name_16_length); //이전에 기억했던 인덱스(폴더명)에 관한 이름을 출력
        push_button(cnt); //다음 파일 실행하는 것이 버튼을 누르는 것과 동일하다
        return;
      }
    }
    if(pass == (maxsize - 1) - cnt){ //다 pass 했으면 stoopped 상태로 하고 함수 종료
      OCR2A = 0; //멈췄으니 pwm생성 x
      OCR2B = 0;
      OCR4A = 0;
      OCR4B = 0;
      TIMSK1 &= ~_BV(OCIE1A);
      EIFR |= _BV(INTF3); //flag clear(1을 사용해 clear한다)
      EIMSK |= _BV(INT3);
      LCD_play2stop();
      stop_song = true;
      EIMSK &= ~_BV(INT0);
      EIMSK &= ~_BV(INT5);
      return; 
    }
  }
  else{ //폴더 안에 파일 재생 시에는 다 파일임을 고려하면 index maxsize-1을 넘지않으면 바로 다음 파일을 재생
    if(cnt + 1 < maxsize - 1){
      cnt = cnt + 1; //index update
      char name_16_length[17]; //이름을 16글자로 줄여야(17번째는 null 사용)
      strncpy(name_16_length, full_name[cnt], sizeof(name_16_length)); //폴더 이름을 다시 복사(LCD에 출력해야하므로)
      name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
      LCD_string(0x80, "                "); //이름 탐색 후 첫번째줄 초기화
      LCD_string(0x80, name_16_length); //이전에 기억했던 인덱스(폴더명)에 관한 이름을 출력
      push_button(cnt); //다음 파일 실행하는 것이 버튼을 누르는 것과 동일하다
      return;
    }
    else{ //더이상 재생할 파일이 없다
      OCR2A = 0; //멈췄으니 pwm생성 x
      OCR2B = 0;
      OCR4A = 0;
      OCR4B = 0;
      TIMSK1 &= ~_BV(OCIE1A);
      EIFR |= _BV(INTF3); //flag clear(1을 사용해 clear한다)
      EIMSK |= _BV(INT3);
      EIMSK &= ~_BV(INT0);
      EIMSK &= ~_BV(INT5);
      LCD_play2stop();
      stop_song = true;
      return;
    }
  }
}
void playing_update(float j){ //LCD 퍼센트 출력
  float percent = j / (float)sub_chunk_2_size * 100.0; //pecent 계산하기
  char p[6];
  dtostrf(percent, 0, 1, p); //문자열로 변경
  LCD_string(0xCA, p); //소숫점 하나까지
  LCD_string(0xCF, "%");
}
void read_buf_2_make_pwm(){ //pwm을 만드는 과정
  if(sample_total_number > sub_chunk_2_size) final = true;
  if(num_channels == 1){ //mono라면
    if(bits_per_sample == 16){ //16bit라면
      if(sample_number < buf_read[buf_index]){ //아직 다 사용 x
        uint16_t out = ((int16_t)buf[buf_index][sample_number] | (int16_t)(buf[buf_index][sample_number + 1]<<8)) + 0x8000; //숫자는 little endian임을 고려, 뒤에 있는 것이 msb이므로 2^8해주기(shift), 16bit의 경우 unsigned로 변환
        OCR2A = out>>8; //pwm_h사용
        OCR2B = out & 0x00FF; //뒤에 8bit 사용(pwm_l)
        OCR4A = out>>8; //pwm_h사용
        OCR4B = out & 0x00FF; //뒤에 8bit 사용(pwm_l)
        sample_number += 2; //16bit의 경우 2바이트를 사용한다
        sample_total_number += 2;
        play_offset += 2; //나중에 멈추고 다시 play할 때 사용
        return;
      }
      else{
        buffer_need_filling[buf_index] = true; //다썼으니 채워줘야함 
        return;
      }
    }
    else{ //8bit라면 --> H 부분은 0이다!, 버퍼 내용을 바로 사용하면 된다(unsigned 8-bit이므로)
      if(sample_number < buf_read[buf_index]){
        OCR2A = 0;
        OCR2B = buf[buf_index][sample_number]; //8BIT의 경우 버퍼 각 위치에 있는 정보를 바로 사용하면 된다.
        OCR4A = 0;
        OCR4B = buf[buf_index][sample_number];
        sample_number += 1; //8bit의 경우 1바이트를 사용한다
        sample_total_number += 1;
        play_offset += 1; //나중에 멈추고 다시 play할 때 사용
        return;
      }
      else{
        buffer_need_filling[buf_index] = true; //다썼으니 채워줘야함
        return;
      }
    }
  }
  else{ //stereo라면
    if(bits_per_sample == 16){ //16bit라면
      if(sample_number < buf_read[buf_index]){ //아직 다 사용 x
        uint16_t out_L = ((int16_t)buf[buf_index][sample_number] | (int16_t)(buf[buf_index][sample_number + 1]<<8)) + 0x8000; //왼쪽 먼저, 숫자는 little endian임을 고려, 뒤에 있는 것이 msb이므로 2^8해주기(shift), 16bit의 경우 unsigned로 변환
        uint16_t out_R = ((int16_t)buf[buf_index][sample_number+2] | (int16_t)(buf[buf_index][sample_number + 3]<<8)) + 0x8000; //오른쪽, 숫자는 little endian임을 고려, 뒤에 있는 것이 msb이므로 2^8해주기(shift), 16bit의 경우 unsigned로 변환
        OCR2A = out_L>>8; //pwm_h(L)사용
        OCR2B = out_L & 0x00FF; //뒤에 8bit 사용(pwm_l(L))
        OCR4A = out_R>>8; //pwm_h(R)사용
        OCR4B = out_R & 0x00FF; //뒤에 8bit 사용(pwm_l(R))
        sample_number += 4; //stereo 16bit의 경우 4바이트를 사용한다
        sample_total_number += 4;
        play_offset += 4; //나중에 멈추고 다시 play할 때 사용
        return;
      }
      else{
        buffer_need_filling[buf_index] = true; //다썼으니 채워줘야함
        return;
      } 
    }
    else{ //8bit라면 --> H 부분은 0이다!, 버퍼 내용을 바로 사용하면 된다(unsigned 8-bit이므로)
      if(sample_number < buf_read[buf_index]){
        OCR2A = 0;
        OCR2B = buf[buf_index][sample_number]; //8BIT의 경우 버퍼 각 위치에 있는 정보를 바로 사용하면 된다.
        OCR4A = 0;
        OCR4B = buf[buf_index][sample_number + 1];
        sample_number += 2; //stereo 8bit의 경우 2바이트를 사용한다
        sample_total_number += 2;
        play_offset += 2; //나중에 멈추고 다시 play할 때 사용
        return;
      }
      else{
        buffer_need_filling[buf_index] = true; //다썼으니 채워줘야함
        return;
      }
    }
  }
}
//노래를 재생하기 전 버퍼를 채워주는 함수
void fill_buffer(const char *target_file){
  if(file.isOpen()) file.close(); //이전에 열었던 file이 있으면 닫고 다시 open
  if(!file.open(target_file, O_RDONLY)) { //인자로 받은 파일을 open 여부 확인(seek, read를 위해)
    //Serial.println("file open failed for play");
    return;
  }
  if(first_play){
    //Serial.print(target_file);
    //Serial.println(" 다음 위치에 있는 파일을 연주");
    file.seek(44); //seek 시작 첫 연주시는 44부터
    buf_read[0] = file.read(buf[0], 3000); //첫 번째 버퍼로 가져오기 + 얼마큼 가져왔는지 확인(read는 return 값으로 읽은 개수를 return한다)
    buf_read[1] = file.read(buf[1], 3000); //두 번쨰 버퍼로 가져오기 + 얼마큼 가져왔는지 확인(read는 return 값으로 읽은 개수를 return한다)
    buf_index = 0; //0번 버퍼부터 읽을 것이다
    sample_number = 0; //현재는 버퍼 하나도 사용 안함
    sample_total_number = 0;
    buffer_need_filling[0] = false; //일단 채웠으므로
    buffer_need_filling[1] = false;
    first_play = false; //정보를 얻고 연주를 하므로 false로 변경, 나중에 연주가 다 되거나 or 로터리 엔코더를 회전시키면 true로 변경
  }
  if(buffer_need_filling[0] || buffer_need_filling[1]){ //버퍼를 채워야한다면
    file.seek(play_offset); //seek 시작
    if(buffer_need_filling[0]){
      buf_read[0] = file.read(buf[0], 3000); //첫 번째 버퍼로 가져오기 + 얼마큼 가져왔는지 확인(read는 return 값으로 읽은 개수를 return한다)
      buf_index = 1; //버퍼 0을 채우는 상황이라면 1을 이제 읽어야 한다
      buffer_need_filling[0] = false;
      sample_number = 0; //둘 중 하나라도 채워야하는 상황이라면 다른 버퍼를 처음부터 읽어야 한다
      return;
    }
    else{
      buf_read[1] = file.read(buf[1], 3000); //두 번쨰 버퍼로 가져오기 + 얼마큼 가져왔는지 확인(read는 return 값으로 읽은 개수를 return한다)
      buf_index = 0; //버퍼 1을 채우는 상황이라면 0을 이제 읽어야 한다
      buffer_need_filling[1] = false;
      sample_number = 0; //둘 중 하나라도 채워야하는 상황이라면 다른 버퍼를 처음부터 읽어야 한다
      return;
    }
  } 
}

void setup(){
  Serial.begin(115200); //오류 결과 등등 시리얼 모니터로 확인하고 싶으면 baud rate 설정해야함
  DDRC |= 0xFF; //PC0~7까지 OUTPUT으로 설정
  DDRK &= ~_BV(PK1); //A9(pk1) input
  DDRB |= 0x7F; //PB5, 6 OUTPUT으로(RS, E), PB4(PWM_H(L)), pb0~3(ss, sck, mosi, miso)
  DDRF |= 0xFF; //PFx OUTPUT으로
  DDRD &= ~0x0F; //18, 19, 20번 핀 input(PW_A, PW_B, ENC_SW):pd1~3, SW1과 연결되어 있는 Pd0도 input으로
  DDRH |= 0x58; //PH3, 4, 6번 OUTPUT으로(PWM_H(R), PWM_L(R), PWM_L(L))
  DDRE &= ~_BV(PE5); //PE5 INPUT --SW2
  delay(50); //initialize 과정 1번
  LCD_init(); //initialize 과정 2~5번
  SD_init(); //SD card를 초기화하고 SD card에서 이름의 정보를 얻어 LCD에 출력을 하기 위해서는 출력 전 이름을 다 알고 있어야 한다
  update_name("/"); //맨 처음에는 SD card 최상위 위치("/")를 open하여 안에 있는 폴더 or 파일 이름을 update하기 위해
  char name_16_length[17]; //이름을 16글자로 줄여야(17번째는 null 사용)
  strncpy(name_16_length, full_name[0], sizeof(name_16_length)); //이름 복사
  name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
  LCD_string(0x80, name_16_length); //initialize 과정 6~7번, 첫 번째 파일 or 폴더 이름 출력
  LCD_string(0xC0, "STOPPED"); //initialize 과정 6~7번, 맨 처음에는 노래가 실행되지 않으므로 stopped이다
  Ext_interrupt3_init(); //external int3 초기화
  Ext_interrupt1_init(); //external int1 초기화
  timer2_init(); //timer/counter2 초기화
  timer4_init(); //timer/counter4 초기화
  sei(); //enable global interrupt
}
void loop(){
  if(millis()- vr1_change_check_time > 100){ //0.1초마다 실행
    int vol = analogRead(A9); //ADC 값(10-bit) 받기
    int ind = vol / (VOL_MAX / 7.0); //7등분: 0~146-->index0, 147~293-->1, 294~439-->2, 440~585-->3, 586~731-->4, 732~877-->5, 878~1023-->6, index는 0부터, +1넣은 이유는 1023이라면 7이 되는데 그것을 막기 위해
    if(ind > 6) ind = 6; //혹시 인덱스가 7이 넘어가면 가장 마지막 데이터를 이용해 led7개 on
    level = ind + 1; //index + 1 초 만큼 시간 이동
    PORTC = data[ind]; //바로 위 코드에서 구한 index + data array 활용
    vr1_change_check_time = millis();
  }
  if(int0_worked == true){ //sw1 누르면
    back = true;
    int0_worked = false; //나중에 또 다시 사용해야 하므로
    LCD_string(0xCD, " "); //10퍼센트 이상에서 한자리수 퍼센트로 내려올 때 이전 데이터가 남아 퍼센트를 예쁘게 못해줌 그래서 4d부분을 공백으로 초기화 해주고 다시 출력하게 함
    go_back_offset(level);
    //Serial.println("sw1 눌림");
  }
  if(int5_worked == true){ //sw2누르면
    go = true;
    int5_worked = false; //나중에 또 다시 사용해야 하므로
    go_back_offset(level);
    //Serial.println("sw2 눌림");
  }
  if(int3_worked == true){ //external intettupt3가 실행 됐다면
    char name_16_length[17]; //이름을 16글자로 줄여야(17번째는 null 사용)
    strncpy(name_16_length, full_name[cnt], sizeof(name_16_length)); //이름 복사
    name_16_length[16] = '\0'; //맨 마지막(17번째) 널문자 넣어준다
    LCD_string(0x80, "                "); //이전 LCD에 display되는 값들을 지워주는 역할
    LCD_string(0x80, name_16_length); //돌릴 때마다 이름 업데이트
    first_play = true; //회전 시켜 이름을 바꿨다면, 처음부터 음악이 play되므로
    int3_worked = false; //interrupt3가 발생했을 때 이름이 바뀌는 일을 수행한 후 다시 false로 바꿔준다, 다음 interrupt3 발생 시를 위해
    final = false; //돌리면 다음 파일로 가므로
  }
  if(int1_worked == true){ //interrupt1가 실행 됐다면
    int1_worked = false; // 나중에 다시 사용하기 위해
    push_button(cnt);
  }
  if(!stop_song){
    if(buffer_need_filling[0] || buffer_need_filling[1]) fill_buffer(play_path); //노래가 play일 때 두 버퍼를 채워야 할 때 fill_buffer로 가서 다시 버퍼를 채운다.
  }
  if(!stop_song){ //노래가 play일 때
    if(millis() - percent_last_time > 100){ //100ms = 0.1초마다 
      playing_update(sample_total_number); 
      percent_last_time = millis();
    }
  }
  if(final == true){
    first_play = true; //끝나고 나서 다시 처음부터 연주하므로
    stop_song = true; //한 곡이 끝났으므로
    next_play();
  }
}