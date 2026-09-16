#include <tgbot/tgbot.h>
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <memory>

const std::string BOT_TOKEN = std::getenv("BOT_TOKEN") ? std::getenv("BOT_TOKEN") : "";
const std::string YADISK_URL = std::getenv("YADISK_URL") ? std::getenv("YADISK_URL") : "";
const std::string WEBHOOK_HOST = std::getenv("WEBHOOK_HOST") ? std::getenv("WEBHOOK_HOST") : "";
const int PORT = std::getenv("PORT") ? std::stoi(std::getenv("PORT")) : 8080;

std::unordered_map<long long, std::string> user_groups;

// Список всех групп из твоего файла
const std::vector<std::string> GROUPS = {
    "КИТ-ТС-26-1", "КИТ-ТС-26-2", "КИТ-ТС-25-1", "КИТ-ТС-25-2", "КИТ-ТС-24",
    "КИТ-СИСА-26-1", "КИТ-СИСА-26-2", "КИТ-РИС-26-1", "КИТ-РИС-26",
    "КИТ-РБП-26-2", "КИТ-РБП-26", "КИТ-РМП-26-3", "КИТ-РМП-26",
    "КИТ-ВР-26-4", "КИТ-ВР-26",
    "КИТ-ТЭСИС-26-1", "КИТ-ТЭСИС-26-2", "КИТ-ТЭСИС-26-3",
    "КИТ-ОИБАС-26", "КИТ-ИССС-26-1", "КИТ-ИССС-26-2",
    "КИТ-СИСА-25-1", "КИТ-СИСА-25-2",
    "КИТ-ИСИП-25-1", "КИТ-ИСИП-25-2", "КИТ-ИСИП-25-3", 
    "КИТ-ИСИП-25-4", "КИТ-ИСИП-25-5", "КИТ-ИСИП-25-6",
    "КИТ-ОИБАС-25", "КИТ-ИССС-25",
    "КИТ-СИСА-24-1", "КИТ-СИСА-24-2",
    "КИТ-ИСИП-24-1", "КИТ-ИСИП-24-2", "КИТ-ИСИП-24-3",
    "КИТ-ИСИП-24-4", "КИТ-ИСИП-24-5", "КИТ-ИСИП-24-6",
    "КИТ-ОИБАС-24", "КИТ-ИССС-24"
};

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

std::string fetch_yadisk_file() {
    std::string api_url = "https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=" + YADISK_URL;
    std::string response = http_get(api_url);
    size_t pos = response.find("\"href\":\"");
    if (pos == std::string::npos) return "";
    pos += 8;
    size_t end_pos = response.find("\"", pos);
    std::string download_link = response.substr(pos, end_pos - pos);
    return http_get(download_link);
}

// Получение текстовой даты (например, "17 сентября") с учетом Якутского времени (+9)
std::string get_text_date(int days_offset) {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::gmtime(&t);
    tm.tm_hour += 9; // Якутск
    tm.tm_mday += days_offset;
    std::mktime(&tm);

    const char* months[] = {
        "января", "февраля", "марта", "апреля", "мая", "июня",
        "июля", "августа", "сентября", "октября", "ноября", "декабря"
    };

    return std::to_string(tm.tm_mday) + " " + months[tm.tm_mon];
}

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Умный парсер под твой Markdown-файл
std::string parse_schedule(const std::string& full_text, const std::string& text_date, const std::string& group) {
    // 1. Ищем блок нужной даты по тексту (например, "17 сентября")
    size_t date_start = full_text.find(text_date);
    if (date_start == std::string::npos) {
        return "❌ Расписание на эту дату не найдено в файле.";
    }

    // 2. Ищем начало следующего блока даты, чтобы ограничить поиск
    // Будем искать следующее вхождение " сентября", " октября" и т.д.
    size_t next_date_start = full_text.length();
    const char* months_search[] = {
        " января", " февраля", " марта", " апреля", " мая", " июня",
        " июля", " августа", " сентября", " октября", " ноября", " декабря"
    };
    
    for (const auto& m : months_search) {
        size_t pos = full_text.find(m, date_start + text_date.length());
        if (pos != std::string::npos && pos < next_date_start) {
            // Отматываем назад до цифры дня
            size_t day_pos = pos;
            while(day_pos > 0 && isdigit(full_text[day_pos-1])) day_pos--;
            if (day_pos < next_date_start) next_date_start = day_pos;
        }
    }

    std::string day_block = full_text.substr(date_start, next_date_start - date_start);

    // 3. Ищем группу внутри этого дня
    // В файле бывает "| КИТ-СИСА-26-1 - 33" или "| КИТ-РБП-26  30"
    std::string group_marker1 = "| " + group + " -";
    std::string group_marker2 = "| " + group + " ";
    
    size_t group_pos = day_block.find(group_marker1);
    if (group_pos == std::string::npos) {
        group_pos = day_block.find(group_marker2);
    }

    if (group_pos == std::string::npos) {
        return "😴 У группы " + group + " нет пар на эту дату.";
    }

    // 4. Ищем конец блока группы (начало следующей группы "КИТ-")
    size_t end_pos = day_block.find("\n| КИТ-", group_pos + 10);
    if (end_pos == std::string::npos) {
        end_pos = day_block.find("\nКИТ-", group_pos + 10); // На случай если нет палки
    }
    if (end_pos == std::string::npos) end_pos = day_block.length();

    std::string raw_schedule = day_block.substr(group_pos, end_pos - group_pos);

    // 5. Парсим строки таблицы
    std::string clean_text = "📅 *Расписание на " + text_date + "*\n🎓 *" + group + "*\n\n";
    std::istringstream stream(raw_schedule);
    std::string line;
    bool has_lessons = false;

    while (std::getline(stream, line)) {
        if (line.find("---") != std::string::npos) continue;
        if (line.find("СТУДЕНТ") != std::string::npos) continue;
        if (line.find("Время") != std::string::npos) continue;
        if (line.find("Расписания учебных занятий") != std::string::npos) continue;
        if (line.find("Курс") != std::string::npos) continue;

        std::vector<std::string> columns;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, '|')) {
            columns.push_back(trim(item));
        }

        // Структура: [0: ПН/ВТ/СР], [1: Время], [2: Предмет], [3: Вид], [4: Препод], [5: Ауд]
        if (columns.size() >= 6) {
            std::string time_str = columns[1];
            std::string subject = columns[2];
            std::string room = columns[5];

            // Если есть время и предмет (игнорируем пустые окна и странные записи без предмета)
            if (time_str.find("-") != std::string::npos && !subject.empty()) {
                has_lessons = true;
                clean_text += "⏰ `" + time_str + "`\n";
                clean_text += "📖 " + subject + "\n";
                if (!room.empty()) {
                    clean_text += "📍 Ауд: " + room + "\n";
                }
                clean_text += "\n";
            }
        }
    }

    if (!has_lessons) {
        return "😴 У группы " + group + " нет пар на эту дату (одни окна).";
    }

    return clean_text;
}

int main() {
    if (BOT_TOKEN.empty()) {
        std::cerr << "Ошибка: Не задан BOT_TOKEN!" << std::endl;
        return 1;
    }

    TgBot::Bot bot(BOT_TOKEN);

    bot.getEvents().onCommand("start", [&bot](TgBot::Message::Ptr message) {
        auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        
        for (const auto& group : GROUPS) {
            auto btn = std::make_shared<TgBot::InlineKeyboardButton>();
            btn->text = group;
            btn->callbackData = "sel_" + group;
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

    bot.getEvents().onCallbackQuery([&bot](TgBot::CallbackQuery::Ptr query) {
        std::string data = query->data;
        long long chatId = query->message->chat->id;
        
        if (data.substr(0, 4) == "sel_") {
            std::string selected_group = data.substr(4);
            user_groups[chatId] = selected_group;
            
            bot.getApi().editMessageText(
                "✅ Выбрана группа: *" + selected_group + "*\n\n"
                "Теперь используй команды:\n"
                "/today — расписание на сегодня\n"
                "/tomorrow — на завтра\n"
                "/dayafter — на послезавтра",
                chatId, query->message->messageId, "", "Markdown");
                
            bot.getApi().answerCallbackQuery(query->id, "Группа сохранена!");
        }
    });

    auto handle_schedule = [&](TgBot::Message::Ptr message, int days_offset) {
        long long chatId = message->chat->id;
        
        if (user_groups.find(chatId) == user_groups.end()) {
            bot.getApi().sendMessage(chatId, "⚠️ Сначала выбери группу через /start");
            return;
        }
        
        std::string group = user_groups[chatId];
        std::string text_date = get_text_date(days_offset);
        
        bot.getApi().sendMessage(chatId, "⏳ Загружаю расписание...");
        
        std::string file_content = fetch_yadisk_file();
        if (file_content.empty()) {
            bot.getApi().sendMessage(chatId, "❌ Ошибка загрузки файла с Яндекс.Диска");
            return;
        }
        
        std::string schedule = parse_schedule(file_content, text_date, group);
        
        if (schedule.length() > 4000) {
            schedule = schedule.substr(0, 4000) + "\n\n... (расписание обрезано)";
        }
        
        bot.getApi().sendMessage(chatId, schedule, false, 0, nullptr, "Markdown");
    };

    bot.getEvents().onCommand("today", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 0); });
    bot.getEvents().onCommand("tomorrow", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 1); });
    bot.getEvents().onCommand("dayafter", [&](TgBot::Message::Ptr msg) { handle_schedule(msg, 2); });

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