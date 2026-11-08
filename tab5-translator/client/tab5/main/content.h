// PAPP OS content: phrasebook + "Take me to" cards (built in; SD can add more, see home.c).
// Chinese/pinyin checked by hand 2026-11-07; addresses only where known — `ok=false` cards show
// "confirmar dirección" so nobody gets driven to a guessed address.
#pragma once
typedef struct { const char *zh, *py, *es, *en; } phrase_t;
typedef struct { const char *cat_es, *cat_en; int first, count; } phrase_cat_t;
typedef struct { const char *zh, *py, *en, *addr_zh; int ok; } place_t;

static const phrase_t PHRASES[] = {
    // 0 Básico
    { "你好", "nǐ hǎo", "Hola", "Hello" },
    { "谢谢", "xièxie", "Gracias", "Thank you" },
    { "不好意思", "bù hǎo yìsi", "Disculpe", "Excuse me" },
    { "对不起", "duìbuqǐ", "Lo siento", "Sorry" },
    { "我不会说中文", "wǒ bú huì shuō zhōngwén", "No hablo chino", "I don't speak Chinese" },
    { "请再说一遍", "qǐng zài shuō yí biàn", "¿Puede repetirlo?", "Please say it again" },
    { "请说慢一点", "qǐng shuō màn yìdiǎn", "Más despacio, por favor", "Please speak slowly" },
    { "我是墨西哥人", "wǒ shì mòxīgē rén", "Soy mexicano", "I'm Mexican" },
    // 8 Comida
    { "我要这个", "wǒ yào zhège", "Quiero esto", "I'd like this" },
    { "不要辣", "bú yào là", "Sin picante", "Not spicy" },
    { "微辣", "wēi là", "Poco picante", "Mildly spicy" },
    { "请给我菜单", "qǐng gěi wǒ càidān", "El menú, por favor", "The menu, please" },
    { "买单", "mǎidān", "La cuenta, por favor", "The bill, please" },
    { "有素食吗？", "yǒu sùshí ma?", "¿Hay comida vegetariana?", "Any vegetarian food?" },
    { "一瓶水", "yì píng shuǐ", "Una botella de agua", "A bottle of water" },
    { "好吃！", "hǎochī!", "¡Delicioso!", "Delicious!" },
    // 16 Transporte
    { "请带我去这个地址", "qǐng dài wǒ qù zhège dìzhǐ", "Lléveme a esta dirección", "Please take me to this address" },
    { "地铁站在哪里？", "dìtiězhàn zài nǎlǐ?", "¿Dónde está el metro?", "Where is the metro station?" },
    { "多少钱？", "duōshao qián?", "¿Cuánto cuesta?", "How much is it?" },
    { "请在这里停车", "qǐng zài zhèlǐ tíngchē", "Pare aquí, por favor", "Please stop here" },
    { "洗手间在哪里？", "xǐshǒujiān zài nǎlǐ?", "¿Dónde está el baño?", "Where is the restroom?" },
    // 21 Hotel
    { "我有预订", "wǒ yǒu yùdìng", "Tengo una reservación", "I have a reservation" },
    { "请给我无线网密码", "qǐng gěi wǒ wúxiànwǎng mìmǎ", "La contraseña del Wi-Fi, por favor", "The Wi-Fi password, please" },
    { "请帮我叫一辆出租车", "qǐng bāng wǒ jiào yí liàng chūzūchē", "Pídame un taxi, por favor", "Please call me a taxi" },
    // 24 Compras
    { "太贵了", "tài guì le", "Está muy caro", "Too expensive" },
    { "可以便宜一点吗？", "kěyǐ piányi yìdiǎn ma?", "¿Me hace descuento?", "Can you make it cheaper?" },
    { "可以用微信支付吗？", "kěyǐ yòng wēixìn zhīfù ma?", "¿Puedo pagar con WeChat Pay?", "Can I pay with WeChat Pay?" },
    { "我只是看看", "wǒ zhǐshì kànkan", "Solo estoy viendo", "Just looking" },
    { "有充电线吗？", "yǒu chōngdiàn xiàn ma?", "¿Tiene cable de carga?", "Do you have a charging cable?" },
    // 29 Emergencia
    { "救命！", "jiùmìng!", "¡Ayuda!", "Help!" },
    { "请叫救护车", "qǐng jiào jiùhùchē", "Llame una ambulancia", "Please call an ambulance" },
    { "我需要医生", "wǒ xūyào yīshēng", "Necesito un médico", "I need a doctor" },
    { "我迷路了", "wǒ mílù le", "Estoy perdido", "I'm lost" },
    { "我的护照丢了", "wǒ de hùzhào diū le", "Perdí mi pasaporte", "I lost my passport" },
    // 34 M5Stack / negocios
    { "很高兴认识你", "hěn gāoxìng rènshi nǐ", "Mucho gusto", "Nice to meet you" },
    { "这是我用 Tab5 做的翻译器", "zhè shì wǒ yòng Tab5 zuò de fānyìqì", "Este es un traductor que hice con la Tab5", "This is a translator I built on the Tab5" },
    { "它在 ESP32-P4 上运行", "tā zài ESP32-P4 shàng yùnxíng", "Corre en el ESP32-P4", "It runs on the ESP32-P4" },
    { "我们可以合作吗？", "wǒmen kěyǐ hézuò ma?", "¿Podemos colaborar?", "Can we work together?" },
    { "可以加个微信吗？", "kěyǐ jiā ge wēixìn ma?", "¿Nos agregamos en WeChat?", "Shall we add each other on WeChat?" },
    { "干杯！", "gānbēi!", "¡Salud!", "Cheers!" },
};
#define PHRASE_COUNT (int)(sizeof PHRASES / sizeof PHRASES[0])
static const phrase_cat_t PHRASE_CATS[] = {
    { "Básico", "Basics", 0, 8 }, { "Comida", "Food", 8, 8 }, { "Transporte", "Getting around", 16, 5 },
    { "Hotel", "Hotel", 21, 3 }, { "Compras", "Shopping", 24, 5 }, { "Emergencia", "Emergency", 29, 5 },
    { "M5Stack", "Business", 34, 6 },
};
#define PHRASE_CAT_COUNT (int)(sizeof PHRASE_CATS / sizeof PHRASE_CATS[0])

static const place_t PLACES[] = {
    { "上海明天广场JW万豪酒店", "Shànghǎi Míngtiān Guǎngchǎng JW Wànháo Jiǔdiàn",
      "JW Marriott Shanghai at Tomorrow Square", "上海市黄浦区南京西路399号", 1 },
    { "上海浦东国际机场", "Shànghǎi Pǔdōng Guójì Jīchǎng", "Shanghai Pudong International Airport", "", 1 },
    { "上海虹桥站", "Shànghǎi Hóngqiáo Zhàn", "Shanghai Hongqiao Railway Station", "", 1 },
    { "外滩", "Wàitān", "The Bund", "", 1 },
    { "深圳宝安JW万豪酒店", "Shēnzhèn Bǎo'ān JW Wànháo Jiǔdiàn", "JW Marriott Shenzhen Bao'an", "", 0 },
    { "深圳宝安国际机场", "Shēnzhèn Bǎo'ān Guójì Jīchǎng", "Shenzhen Bao'an International Airport", "", 1 },
    { "深圳北站", "Shēnzhèn Běi Zhàn", "Shenzhen North Railway Station", "", 1 },
    { "华强北", "Huáqiángběi", "Huaqiangbei electronics market", "", 1 },
    { "深圳国际会展中心", "Shēnzhèn Guójì Huìzhǎn Zhōngxīn", "Shenzhen World (CHTF venue?)", "", 0 },
    { "明栈信息科技（M5Stack）", "Míngzhàn Xìnxī Kējì", "M5Stack office", "", 0 },
};
#define PLACE_COUNT (int)(sizeof PLACES / sizeof PLACES[0])
