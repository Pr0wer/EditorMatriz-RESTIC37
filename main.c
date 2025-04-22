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
#define COLOR_COUNT 4

// Pinos
const uint btn_a_pin = 5;
const uint btn_b_pin = 6;
const uint led_red_pin = 13;
const uint led_green_pin = 11;
const uint led_blue_pin = 12;
const uint joystick_x_pin = 27;
const uint joystick_y_pin = 26;
const uint joystick_btn_pin = 22;
const uint buzzer_pin = 21;

// Valores do PWM do buzzer
const uint16_t WRAP = 5000;   
const float DIV = 0.0;
uint sliceBuzzer;

// Variáveis para o quadrado no Display SSD1306
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

// Booleanas de estado
static volatile bool joystick_moveu = false;
static volatile bool modoEdicao = true;
static volatile bool modoAnterior = true;
static volatile bool buzzerOn = true;

// Estrutura para criação e locomoção do cursor na matriz de LEDs
typedef struct cursor
{
    int linha;
    int coluna;
    Rgb cor;
} cursor;
cursor c;

// Buffer para armazenar o desenho atual
Rgb desenhoAtual[MATRIZ_ROWS][MATRIZ_COLS] = 
{
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}
};

// Cursor para alternar entre as diferentes cores no vetor
int8_t corAtual = 0;
Rgb cores[COLOR_COUNT] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
const uint leds[LED_COUNT] = {led_red_pin, led_green_pin, led_blue_pin};

// Armazenar informações da comunicação serial
char recebido;
uint digito;

// Tratamento do efeito bounce
static volatile uint tempoAnterior = 0;

// Headers de função
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
        // Ler dados das coordenadas X e Y do joystick e converter em variação
        adc_select_input(1);
        joystick_dx = adc_read() - CENTRAL_X;
        adc_select_input(0);
        joystick_dy = adc_read()- CENTRAL_Y;

        // Tratar possíveis variações da posição central do joystick
        tratarVariacao(&joystick_dx, JS_VARIACAO);
        tratarVariacao(&joystick_dy, JS_VARIACAO);

        // Desenha o quadrado no Display baseado na vvariação do joystick
        desenharQuadrado();

        // Se o joystick se moveu, não está em delay e o programa está no Modo de Edição
        if ((joystick_dx != 0 || joystick_dy != 0) && !joystick_moveu && modoEdicao)
        {   
            printf("Movimento do joystick detectado! Delay do joystick em execução..\n");
            // Atualizar posição do cursor
            moverCursor(joystick_dx, joystick_dy);
            printf("Cursor na matriz movido!\n");

            // Inicializar delay do movimento
            joystick_moveu = true;
            add_alarm_in_ms(300, delay_callback, NULL, false);
        }

        // Se o modo de execução foi alterado
        if (modoAnterior != modoEdicao)
        {    
            // Atualizar a matriz de LEDs
            atualizarDesenho();
            modoAnterior = modoEdicao;
        }

        // Esperar e armazenar informações enviadas via UART
        recebido = getchar_timeout_us(100000);
        if (recebido != PICO_ERROR_TIMEOUT && isdigit(recebido))
        {   
            // Converter caractere contendo digito em um número inteiro
            digito = recebido - '0';

            // Ligar ou desligar funcionalidade do buzzer baseado no digito
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

// Inicializa os botões
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

// Inicializa os LEDs
void inicializarLED() 
{
    for (int i = 0; i < LED_COUNT; i++)
    {
        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);

        // Garantir que a cor selecionada comece ligada
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

            // Desligar LED RGB atualmente ligado
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

            // Mover cursor para a próxima cor
            corAtual++;
            corAtual %= 4;

            // Ligar LED se o corAtual apontar para um
            if (corAtual < 3)
            {
                gpio_put(leds[corAtual], 1);
            }

        }
        else if (gpio == btn_b_pin && modoEdicao)
        {   
            printf("Botão B pressionado!\n");

            // Substitui a cor definida no LED do cursor pela selecionada
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
            // Alterna entre os modos de execução
            modoEdicao = !modoEdicao;
            printf(modoEdicao ? "EDICAO\n" : "VISUALIZACAO\n");

        }
    }
}

void desenharQuadrado()
{    
    // Obter novas coordenadas do quadrado
    pixel_x_novo = CENTRAL_PIXEL_X + round(joystick_dx / (float) jsx_por_pixel);
    pixel_y_novo = CENTRAL_PIXEL_Y - round(joystick_dy / (float) jsy_por_pixel);

    // Limitar valor das coordenadas entre as extremidades
    limitarCoord(&pixel_x_novo, 0, DISPLAY_WIDTH - SQUARE_WIDTH);
    limitarCoord(&pixel_y_novo, 0, DISPLAY_HEIGHT - SQUARE_HEIGHT);

    // Se as novas coordenadas obtidas são diferentes
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

// Exibe o desenho, com ou sem cursor, na matriz de LEDs
void atualizarDesenho()
{       
    if (modoEdicao)
    {    
        // Garantir a troca sem perda de dados entre a cor estabelecida no desenho e a do cursor
        // antes de atualizar
        Rgb temp = desenhoAtual[c.linha][c.coluna];
        desenhoAtual[c.linha][c.coluna] = c.cor;
        desenharMatriz(desenhoAtual);
        desenhoAtual[c.linha][c.coluna] = temp;
    }
    else
    {    
        // Apenas atualizar a matriz
        desenharMatriz(desenhoAtual);
    }
}

// Move o cursor pela matriz de LEDs
void moverCursor(int16_t dx, int16_t dy)
{    
    // Incrementar ou decrementar coordenada X, se houve variação no joystick
    if (dx > 0)
    {
        c.coluna++;
    }
    else if (dx < 0)
    {
        c.coluna--;
    }

    // Incrementar ou decrementar coordenada Y, se houve variação no joystick
    if (dy > 0)
    {
        c.linha--;
    }
    else if (dy < 0)
    {
        c.linha++;
    }

    // Limitar coordenadas para as extremidades da matriz
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

// Tratamento do delay do movimento do cursor
int64_t delay_callback(alarm_id_t id, void *user_data)
{
    joystick_moveu = false;
    printf("Delay do joystick finalizado!\n");
    return 0;
}

// Tratamento da duração do buzzer
int64_t buzzer_off_callback(alarm_id_t id, void *user_data)
{
    pwm_set_gpio_level(buzzer_pin, 0);
    printf("Buzzer desativado!\n");
    return 0;
}

