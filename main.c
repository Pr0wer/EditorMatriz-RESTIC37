#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include "hardware/adc.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "lib/ws2812b.h"
#include "lib/ssd1306.h"

#define CENTRAL_X 2048
#define CENTRAL_Y 2048
#define JS_VARIACAO 180
#define SQUARE_WIDTH 8
#define SQUARE_HEIGHT 8
#define CENTRAL_PIXEL_X 61
#define CENTRAL_PIXEL_Y 29

#define LED_COUNT 3

const uint btn_a_pin = 5;
const uint btn_b_pin = 6;
const uint led_red_pin = 13;
const uint led_green_pin = 11;
const uint led_blue_pin = 12;
const uint joystick_x_pin = 27;
const uint joystick_y_pin = 26;
const uint joystick_btn_pin = 22;
const uint buzzer_pin = 21;

// Variáveis para funcionamento do buzzer
const uint16_t WRAP = 5000;   
const float DIV = 0.0;
uint sliceBuzzer;
static volatile bool buzzerOn = true;

ssd1306_t ssd;
static volatile uint16_t pixel_x = CENTRAL_PIXEL_X;
static volatile uint16_t pixel_y = CENTRAL_PIXEL_Y;
static volatile uint16_t pixel_x_novo;
static volatile uint16_t pixel_y_novo;

// Valores para conversão da variação do joystick para variação de pixels no display
const uint16_t jsx_por_pixel = CENTRAL_X / ((DISPLAY_WIDTH - SQUARE_WIDTH) / 2.0);
const uint16_t jsy_por_pixel = CENTRAL_Y / ((DISPLAY_HEIGHT - SQUARE_HEIGHT) / 2.0);

// Variação do joystick (X e Y)
static int16_t joystick_dx, joystick_dy = 0;

static volatile bool joystick_moveu = false;
static volatile bool modoEdicao = true;
static volatile bool modoAnterior = true;

typedef struct cursor
{
    int linha;
    int coluna;
    Rgb cor;
} cursor;

cursor c;

Rgb desenhoAtual[MATRIZ_ROWS][MATRIZ_COLS] = 
{
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}
};

int8_t corAtual = 0;
Rgb cores[4] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
const uint leds[3] = {led_red_pin, led_green_pin, led_blue_pin};

char recebido;
uint digito;
static volatile bool buzzerOn;

static volatile uint tempoAnterior = 0;

void inicializarBtn();
void inicializarLED();
uint inicializarPWM(uint pino);
void btn_handler(uint gpio, uint32_t events);
void tratarVariacao(int16_t *valor, uint16_t variacao);
void limitarCoord(volatile int16_t *valor, uint min, uint max);
void desenharQuadrado();
void atualizarDesenho();
void moverCursor(int16_t dx, int16_t dy);
int64_t delay_callback(alarm_id_t id, void *user_data);
int64_t buzzer_off_callback(alarm_id_t id, void *user_data);

int main()
{   
    // Inicializar botões
    inicializarBtn();

    // Inicializar LED RGB
    inicializarLED();

    // Configurar PWM para o buzzer
    sliceBuzzer = inicializarPWM(buzzer_pin);
    pwm_set_enabled(sliceBuzzer, true);

    // Inicializar ADC para o joystick
    adc_init();
    adc_gpio_init(joystick_x_pin);
    adc_gpio_init(joystick_y_pin);

    // Configurar display SSD1306 e desenhar quadrado
    ssd1306_i2c_init(&ssd);
    ssd1306_fill(&ssd, false);
    ssd1306_rect(&ssd, pixel_y, pixel_x, SQUARE_WIDTH, SQUARE_HEIGHT, 1, true, true);
    ssd1306_send_data(&ssd);

    // Definir posição inicial e cor do cursor
    c.linha = 2;
    c.coluna = 2;
    c.cor.r = 1;
    c.cor.g = 1;
    c.cor.b = 1;
    
    // Configurar matriz de LEDs
    inicializarMatriz();
    atualizarDesenho();

    // Interrupções
    gpio_set_irq_enabled_with_callback(btn_a_pin, GPIO_IRQ_EDGE_FALL, true, &btn_handler);
    gpio_set_irq_enabled_with_callback(btn_b_pin, GPIO_IRQ_EDGE_FALL, true, &btn_handler);
    gpio_set_irq_enabled_with_callback(joystick_btn_pin, GPIO_IRQ_EDGE_FALL, true, &btn_handler);

    stdio_init_all();

    while (true) 
    {   
        adc_select_input(1);
        joystick_dx = adc_read() - CENTRAL_X;
        adc_select_input(0);
        joystick_dy = adc_read()- CENTRAL_Y;

        // Tratar possíveis variações da posição central do joystick
        tratarVariacao(&joystick_dx, JS_VARIACAO);
        tratarVariacao(&joystick_dy, JS_VARIACAO);

        desenharQuadrado();

        if ((joystick_dx != 0 || joystick_dy != 0) && !joystick_moveu && modoEdicao)
        {   
            printf("Movimento do joystick detectado! Delay do joystick em execução..\n");
            moverCursor(joystick_dx, joystick_dy);
            printf("Cursor na matriz movido!\n");
            joystick_moveu = true;
            add_alarm_in_ms(300, delay_callback, NULL, false);
        }

        if (modoAnterior != modoEdicao)
        {
            atualizarDesenho();
            modoAnterior = modoEdicao;
        }
        
        recebido = getchar_timeout_us(100000);
        if (recebido != PICO_ERROR_TIMEOUT && isdigit(recebido))
        {   
            digito = recebido - '0';
            if (digito == 1)
            {
                buzzerOn = true;
                printf("Funcionalidade do buzzer: ON\n");
            }
            else if (digito == 0)
            {
                buzzerOn = false;
                printf("Funcionalidade do buzzer: OFF\n");
            }
            else
            {
                printf("'1' = Ligar buzzer / '0' = Desligar buzzer\n");
            }
        }

        sleep_ms(10);
    }
}

void inicializarBtn()
{
    gpio_init(btn_a_pin);
    gpio_set_dir(btn_a_pin, GPIO_IN);
    gpio_pull_up(btn_a_pin);

    gpio_init(btn_b_pin);
    gpio_set_dir(btn_b_pin, GPIO_IN);
    gpio_pull_up(btn_b_pin);

    gpio_init(joystick_btn_pin);
    gpio_set_dir(joystick_btn_pin, GPIO_IN);
    gpio_pull_up(joystick_btn_pin);
}

void inicializarLED() 
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);

        if (i == corAtual)
        {
            gpio_put(leds[i], 1);
        }
        else
        {
            gpio_put(leds[i], 0);
        }
    }
}

// Inicializa e configura o PWM em um pino
uint inicializarPWM(uint pino)
{   
    // Obter slice e definir pino como PWM
    gpio_set_function(pino, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pino);

    // Configurar frequência
    pwm_set_wrap(slice, WRAP);
    pwm_set_clkdiv(slice, DIV); 

    pwm_set_gpio_level(pino, 0);

    return slice;
}

// Trata uma dada variação presente em um valor
void tratarVariacao(int16_t *valor, uint16_t variacao)
{   
    // Caso o valor esteja dentro da faixa de variação
    if (abs(*valor) < variacao)
    {   
        // Considerar como alteração nula
        *valor = 0;
    }
}

// Limita um valor de uma coordenada entre as extremidades estabelecidas
void limitarCoord(volatile int16_t *valor, uint min, uint max)
{
    if (*valor < min)
    {
        *valor = min;
    }
    else if (*valor > max)
    {
        *valor = max;
    }
}

void btn_handler(uint gpio, uint32_t events)
{
    // Tratar efeito bounce
    uint tempoAtual = to_us_since_boot(get_absolute_time());
    if (tempoAtual - tempoAnterior > 200000)
    {   
        tempoAnterior = tempoAtual;
        if (gpio == btn_a_pin && modoEdicao)
        {   
            printf("Botão A pressionado!\n");
            if (cores[corAtual].r)
            {
                gpio_put(led_red_pin, 0);
            }
            else if (cores[corAtual].g)
            {
                gpio_put(led_green_pin, 0);
            }
            else if (cores[corAtual].b)
            {
                gpio_put(led_blue_pin, 0);
            }

            corAtual++;
            corAtual %= 4;

            if (corAtual < 3)
            {
                gpio_put(leds[corAtual], 1);
            }

        }
        else if (gpio == btn_b_pin && modoEdicao)
        {   
            printf("Botão B pressionado!\n");
            desenhoAtual[c.linha][c.coluna] = cores[corAtual];

            // Emitir sinal sonoro no buzzer por 200 ms
            if (buzzerOn)
            {
                printf("Buzzer ativado!\n");
                pwm_set_gpio_level(buzzer_pin, WRAP / 2);
                add_alarm_in_ms(200, buzzer_off_callback, NULL, false);
            }
        }
        else if (gpio == joystick_btn_pin)
        {   
            printf("Botão do joystick pressionado! Entrando no modo ");
            modoEdicao = !modoEdicao;
            printf(modoEdicao ? "EDICAO\n" : "VISUALIZACAO\n");

        }
    }
}

void desenharQuadrado()
{
    pixel_x_novo = CENTRAL_PIXEL_X + round(joystick_dx / (float) jsx_por_pixel);
    pixel_y_novo = CENTRAL_PIXEL_Y - round(joystick_dy / (float) jsy_por_pixel);

    // Limitar valor das coordenadas entre as extremidades, considerando se existe borda 
    limitarCoord(&pixel_x_novo, 0, DISPLAY_WIDTH - SQUARE_WIDTH);
    limitarCoord(&pixel_y_novo, 0, DISPLAY_HEIGHT - SQUARE_HEIGHT);

    if (pixel_x_novo != pixel_x || pixel_y_novo != pixel_y)
    {
        // Alterar posição atual com a nova
        pixel_x = pixel_x_novo;
        pixel_y = pixel_y_novo;

        // Atualizar posição do quadrado no display
        ssd1306_fill(&ssd, false);
        ssd1306_rect(&ssd, pixel_y, pixel_x, SQUARE_WIDTH, SQUARE_HEIGHT, 1, true, true);
        ssd1306_send_data(&ssd);
    }
}

void atualizarDesenho()
{   
    if (modoEdicao)
    {
        Rgb temp = desenhoAtual[c.linha][c.coluna];
        desenhoAtual[c.linha][c.coluna] = c.cor;
        desenharMatriz(desenhoAtual);
        desenhoAtual[c.linha][c.coluna] = temp;
    }
    else
    {
        desenharMatriz(desenhoAtual);
    }
}

void moverCursor(int16_t dx, int16_t dy)
{
    if (dx > 0)
    {
        c.coluna++;
    }
    else if (dx < 0)
    {
        c.coluna--;
    }
    
    if (dy > 0)
    {
        c.linha--;
    }
    else if (dy < 0)
    {
        c.linha++;
    }

    if (c.linha >= MATRIZ_ROWS)
    {
        c.linha = 0;
    }
    else if (c.linha < 0)
    {
        c.linha = MATRIZ_ROWS - 1;
    }
    if (c.coluna >= MATRIZ_COLS)
    {
        c.coluna = 0;
    }
    else if (c.coluna < 0)
    {
        c.coluna = MATRIZ_COLS - 1;
    }

    atualizarDesenho();
}

int64_t delay_callback(alarm_id_t id, void *user_data)
{
    joystick_moveu = false;
    printf("Delay do joystick finalizado!\n");
    return 0;
}

int64_t buzzer_off_callback(alarm_id_t id, void *user_data)
{
    pwm_set_gpio_level(buzzer_pin, 0);
    printf("Buzzer desativado!\n");
    return 0;
}

