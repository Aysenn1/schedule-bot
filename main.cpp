#include <tgbot/tgbot.h>
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include <ctime>
#include <memory>

// --- Переменные окружения ---
const std::string BOT_TOKEN = std::getenv("BOT_TOKEN") ? std::getenv("BOT_TOKEN") : "";
const std::string YADISK_URL = std::getenv("YADISK_URL") ? std::getenv("YADISK_URL") : "";
const std::string WEBHOOK_HOST = std::getenv("WEBHOOK_HOST") ? std::getenv("WEBHOOK_HOST") : "";
const int PORT = std::getenv("PORT") ? std::stoi(std::getenv("PORT")) : 8080;

// --- Хранилище выбранных групп (chat_id -> group_name) ---
std::unordered_map<long long, std::string> user_groups;

// --- Список групп для кнопок (добавь сюда все свои группы) ---
const std::vector<std::string> GROUPS = {
    "ТС-26-1", "ТС-26-2", "ТС-25-1", "ТС-25-2", "ТС-24",
    "СИСА-26-1", "СИСА-26-2", "РИС-26-1", "РИС-26",
    "РБП-26-2", "РБП-26", "РМП-26-3", "РМП-26",
    "ВР-26-4", "ВР-26",
    "ТЭСИС-26-1", "ТЭСИС-26-2", "ТЭСИС-26-3",
    "ОИБАС-26", "ИССС-26-1", "ИССС-26-2",
    "СИСА-25-1", "СИСА-25-2",
    "ИСИП-25-1", "ИСИП-25-2", "ИСИП-25-3", 
    "ИСИП-25-4", "ИСИП-25-5", "ИСИП-25-6",
    "ОИБАС-25", "ИССС-25",
    "СИСА-24-1", "СИСА-24-2",
    "ИСИП-24-1", "ИСИП-24-2", "ИСИП-24-3",
    "ИСИП-24-4", "ИСИП-24-5", "ИСИП-24-6",
    "ОИБАС-24", "ИССС-24"
};

// --- HTTP запрос через curl ---
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string http_get(const std::string& url) {
    CURL* curl;
    std::string readBuffer;
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.68.0");
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return readBuffer;
}

// --- Скачивание файла с Яндекс.Диска ---
std::string fetch_yadisk_file() {
    std::string api_url = "https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=" + YADISK_URL;
    std::string response = http_get(api_url);
    
    // Простой парсинг JSON без сторонних библиотек (ищем "href":"...")
    size_t pos = response.find("\"href\":\"");
    if (pos == std::string::npos) return "";
    pos += 8;
    size_t end_pos = response.find("\"", pos);
    std::string download_link = response.substr(pos, end_pos - pos);
    
    return http_get(download_link);
}

// --- Получение даты в формате ДДММГГ (как в твоем файле) ---
std::string get_date(int days_offset) {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    tm.tm_mday += days_offset;
    std::mktime(&tm); // Нормализует дату (переносит месяцы/годы)
    
    char buffer[7];
    std::strftime(buffer, sizeof(buffer), "%d%m%y", &tm);
    return std::string(buffer);
}

// --- Поиск расписания в тексте ---
std::string find_schedule(const std::string& full_text, const std::string& date_code, const std::string& group) {
    // 1. Ищем блок нужной даты (например, "170926")
    size_t date_pos = full_text.find(date_code);
    if (date_pos == std::string::npos) return "❌ Расписание на эту дату не найдено в файле.";
    
    // Ищем следующую дату, чтобы ограничить поиск
    // Даты в файле идут как 6 цифр в начале строки. 
    // Для простоты просто ищем следующую группу или конец файла от текущей даты
    size_t next_date_pos = full_text.find("\n|", date_pos + 6); // Упрощенный поиск конца блока
    
    // 2. Ищем группу ВНУТРИ этого временного промежутка
    // Чтобы не искать по всему файлу, сузим область поиска
    std::string search_area = full_text.substr(date_pos); 
    
    std::string group_header = group + " -"; // В файле формат "КИТ-СИСА-26-1 - 33 СТУДЕНТА"
    size_t group_pos = search_area.find(group_header);
    
    if (group_pos == std::string::npos) {
        return "❌ Группа " + group + " не найдена на эту дату.";
    }
    
    // 3. Вырезаем расписание до следующей группы
    // Следующая группа обычно начинается с новой строки и содержит "СТУДЕНТ"
    size_t end_pos = search_area.find("СТУДЕНТ", group_pos + group_header.length());
    if (end_pos != std::string::npos) {
        // Откатываемся назад до начала строки следующей группы
        end_pos = search_area.rfind("\n", end_pos);
    } else {
        end_pos = search_area.length();
    }
    
    std::string raw_schedule = search_area.substr(group_pos, end_pos - group_pos);
    
    // 4. Очистка от Markdown-таблицы (удаляем "|", "---", пустые строки)
    std::string clean_text;
    std::istringstream stream(raw_schedule);
    std::string line;
    while (std::getline(stream, line)) {
        // Удаляем символы таблицы
        line.erase(std::remove(line.begin(), line.end(), '|'), line.end());
        
        // Пропускаем разделители и дублирующиеся заголовки групп
        if (line.find("---") != std::string::npos) continue;
        if (line.find("СТУДЕНТ") != std::string::npos) continue;
        if (line.find("Время") != std::string::npos && line.find("Дисциплина") != std::string::npos) continue;
        
        // Удаляем лишние пробелы по краям
        size_t start = line.find_first_not_of(" \t\n\r");
        size_t end = line.find_last_not_of(" \t\n\r");
        if (start != std::string::npos) {
            clean_text += line.substr(start, end - start + 1) + "\n";
        }
    }
    
    return clean_text.empty() ? "😴 На эту дату пар нет!" : clean_text;
}

int main() {
    if (BOT_TOKEN.empty()) {
        std::cerr << "Ошибка: Не задан BOT_TOKEN!" << std::endl;
        return 1;
    }

    TgBot::Bot bot(BOT_TOKEN);

    // === КОМАНДА /start ===
    bot.getEvents().onCommand("start", [&bot](TgBot::Message::Ptr message) {
        auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
        
        // Создаем кнопки по 2 в ряд
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        for (const auto& group : GROUPS) {
            auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
            btn->text = group;
            btn->callbackData = "select_" + group;
            row.push_back(btn);
            
            if (row.size() == 2) {
                keyboard->inlineKeyboard.push_back(row);
                row.clear();
            }
        }
        if (!row.empty()) keyboard->inlineKeyboard.push_back(row);

        bot.getApi().sendMessage(message->chat->id, 
            "Привет! 👋 Я бот расписания СВФУ.\nВыбери свою группу:", 
            false, 0, keyboard);
    });

    // === ОБРАБОТКА НАЖАТИЯ КНОПОК (выбор группы) ===
    bot.getEvents().onCallbackQuery([&bot](TgBot::CallbackQuery::Ptr query) {
        std::string data = query->data;
        long long chatId = query->message->chat->id;
        
        if (data.substr(0, 7) == "select_") {
            std::string selected_group = data.substr(7);
            
            // Запоминаем группу в памяти
            user_groups[chatId] = selected_group;
            
            // Обновляем сообщение
            bot.getApi().editMessageText(
                "✅ Выбрана группа: *" + selected_group + "*\n\n"
                "Теперь используй команды:\n"
                "/today — расписание на сегодня\n"
                "/tomorrow — на завтра\n"
                "/dayafter — на послезавтра",
                chatId, query->message->messageId, "", "Markdown");
                
            // Отвечаем на callback (чтобы часы загрузки исчезли)
            bot.getApi().answerCallbackQuery(query->id, "Группа сохранена!");
        }
    });

    // === КОМАНДЫ РАСПИСАНИЯ ===
    auto handle_schedule = [&](TgBot::Message::Ptr message, int days_offset) {
        long long chatId = message->chat->id;
        
        // Проверяем, выбрал ли пользователь группу
        if (user_groups.find(chatId) == user_groups.end()) {
            bot.getApi().sendMessage(chatId, "⚠️ Сначала выбери группу через /start");
            return;
        }
        
        std::string group = user_groups[chatId];
        std::string date_code = get_date(days_offset);
        
        bot.getApi().sendMessage(chatId, "⏳ Загружаю расписание...");
        
        std::string file_content = fetch_yadisk_file();
        if (file_content.empty()) {
            bot.getApi().sendMessage(chatId, "❌ Ошибка загрузки файла с Яндекс.Диска");
            return;
        }
        
        std::string schedule = find_schedule(file_content, date_code, group);
        
        // Telegram имеет лимит 4096 символов
        if (schedule.length() > 4000) {
            schedule = schedule.substr(0, 4000) + "\n\n... (расписание обрезано)";
        }
        
        bot.getApi().sendMessage(chatId, schedule);
    };

    bot.getEvents().onCommand("today", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 0); });
    bot.getEvents().onCommand("tomorrow", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 1); });
    bot.getEvents().onCommand("dayafter", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 2); });

    // === Установка Webhook ===
    try {
        std::string webhook_url = WEBHOOK_HOST + "/webhook";
        std::cout << "Setting webhook: " << webhook_url << std::endl;
        bot.getApi().setWebhook(webhook_url);
    } catch (TgBot::TgException& e) {
        std::cerr << "Webhook error: " << e.what() << std::endl;
    }

    std::cout << "Bot started on port " << PORT << std::endl;
    TgBot::TgWebhookTcpServer server(PORT, "/webhook", bot);
    server.start();

    return 0;
}