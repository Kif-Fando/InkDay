#include "i18n.h"
#include <inkview.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  /* Firmware language key, or NULL when the firmware has no suitable text. */
  const char *system_key;
  const char *english;
} StringEntry;

/* Keys were checked against /ebrmain/language/{en,ru}.txt on the device.
   Own translations for strings without system keys will be added here later. */
static const StringEntry strings[STR_COUNT] = {
    [STR_SETTINGS] = {"@Settings", "Settings"},
    [STR_SAVE] = {"@Save", "Save"},
    [STR_CANCEL] = {"@Cancel", "Cancel"},
    [STR_WEATHER_CITY] = {NULL, "Weather / City"},
    [STR_CITY] = {NULL, "City"},
    [STR_SEARCH] = {"@Search", "Search"},
    [STR_SEARCHING] = {NULL, "Searching..."},
    [STR_DETECT_BY_IP] = {NULL, "Detect by IP"},
    [STR_DETECTING] = {NULL, "Detecting..."},
    [STR_DETECTING_LOCATION] = {NULL, "Detecting location..."},
    [STR_UPDATE_INTERVAL] = {"@Update_interval", "Update interval"},
    [STR_MINUTES_SHORT] = {"@LegimiMinPoint", "min"},
    [STR_CONNECTING] = {"@Connecting", "Connecting"},
    [STR_NETWORK_ERROR] = {"@NetworkError", "Network error"},
    [STR_TIMEOUT] = {"@TimeOutError", "Request is timed out"},
    [STR_PREVIOUS_CONNECTION_BUSY] = {NULL, "Previous network connection is still finishing"},
    [STR_CANNOT_START_CONNECTION] = {NULL, "Cannot start network connection"},
    [STR_CANNOT_START_LOOKUP] = {NULL, "Cannot start location lookup"},
    [STR_SEARCH_FAILED] = {NULL, "City search failed"},
    [STR_LOCATION_NOT_DETECTED] = {NULL, "Could not detect a city"},
    [STR_NOTHING_FOUND] = {"@Nothing_found", "Nothing found"},
    [STR_ENTER_CITY] = {NULL, "Enter a city name"},
    [STR_CITY_TOO_LONG] = {NULL, "City name is too long"},
    [STR_SELECT_CITY] = {NULL, "Select a city:"},
    [STR_SELECT_IP_LOCATION] = {NULL, "Approximate IP location: tap to select"},
    [STR_SELECTED] = {NULL, "Selected"},
    [STR_IP_LOCATION] = {NULL, "IP location"},
    [STR_PLEASE_WAIT] = {"@Please_wait", "Please wait..."},
    [STR_SEARCH_AND_SELECT_FIRST] = {NULL, "Search and select a city first"},
    [STR_SETTINGS_UNREADABLE] = {NULL, "Cannot read saved settings. Select a location and save again."},
    [STR_SETTINGS_BUSY] = {NULL, "Cannot apply settings. A weather request may still be running. Try again shortly."},
    [STR_SETTINGS_SAVE_FAILED] = {NULL, "Cannot save settings. Previous settings were kept."},
    [STR_SETTINGS_APPLY_FAILED] = {NULL, "Settings were saved but could not be applied. Restart the app."},
};

/* Application dictionaries for strings without a firmware key.
   Missing entries fall back to English. Machine-assisted; native review welcome. */

/* Russian */
static const char *const lang_ru[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Погода / Город",
    [STR_CITY] = "Город",
    [STR_SEARCHING] = "Поиск...",
    [STR_DETECT_BY_IP] = "Определить по IP",
    [STR_DETECTING] = "Определение...",
    [STR_DETECTING_LOCATION] = "Определение местоположения...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Предыдущее подключение ещё не завершено",
    [STR_CANNOT_START_CONNECTION] = "Не удалось начать подключение к сети",
    [STR_CANNOT_START_LOOKUP] = "Не удалось начать поиск местоположения",
    [STR_SEARCH_FAILED] = "Ошибка поиска города",
    [STR_LOCATION_NOT_DETECTED] = "Не удалось определить город",
    [STR_ENTER_CITY] = "Введите название города",
    [STR_CITY_TOO_LONG] = "Слишком длинное название города",
    [STR_SELECT_CITY] = "Выберите город:",
    [STR_SELECT_IP_LOCATION] = "Примерное местоположение по IP: нажмите, чтобы выбрать",
    [STR_SELECTED] = "Выбрано",
    [STR_IP_LOCATION] = "По IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Сначала найдите и выберите город",
    [STR_SETTINGS_UNREADABLE] = "Не удалось прочитать сохранённые настройки. Выберите город и сохраните снова.",
    [STR_SETTINGS_BUSY] = "Не удалось применить настройки: идёт запрос погоды. Повторите чуть позже.",
    [STR_SETTINGS_SAVE_FAILED] = "Не удалось сохранить настройки. Оставлены прежние.",
    [STR_SETTINGS_APPLY_FAILED] = "Настройки сохранены, но не применены. Перезапустите приложение.",
};

/* Belarusian */
static const char *const lang_be[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Надвор'е / Горад",
    [STR_CITY] = "Горад",
    [STR_SEARCHING] = "Пошук...",
    [STR_DETECT_BY_IP] = "Вызначыць па IP",
    [STR_DETECTING] = "Вызначэнне...",
    [STR_DETECTING_LOCATION] = "Вызначэнне месцазнаходжання...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Папярэдняе падключэнне яшчэ не завершана",
    [STR_CANNOT_START_CONNECTION] = "Не ўдалося пачаць падключэнне да сеткі",
    [STR_CANNOT_START_LOOKUP] = "Не ўдалося пачаць пошук месцазнаходжання",
    [STR_SEARCH_FAILED] = "Памылка пошуку горада",
    [STR_LOCATION_NOT_DETECTED] = "Не ўдалося вызначыць горад",
    [STR_ENTER_CITY] = "Увядзіце назву горада",
    [STR_CITY_TOO_LONG] = "Занадта доўгая назва горада",
    [STR_SELECT_CITY] = "Выберыце горад:",
    [STR_SELECT_IP_LOCATION] = "Прыблізнае месцазнаходжанне па IP: націсніце, каб выбраць",
    [STR_SELECTED] = "Выбрана",
    [STR_IP_LOCATION] = "Па IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Спачатку знайдзіце і выберыце горад",
    [STR_SETTINGS_UNREADABLE] = "Не ўдалося прачытаць захаваныя налады. Выберыце горад і захавайце зноў.",
    [STR_SETTINGS_BUSY] = "Не ўдалося ўжыць налады: ідзе запыт надвор'я. Паўтарыце крыху пазней.",
    [STR_SETTINGS_SAVE_FAILED] = "Не ўдалося захаваць налады. Пакінуты папярэднія.",
    [STR_SETTINGS_APPLY_FAILED] = "Налады захаваны, але не ўжыты. Перазапусціце праграму.",
};

/* Ukrainian */
static const char *const lang_uk[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Погода / Місто",
    [STR_CITY] = "Місто",
    [STR_SEARCHING] = "Пошук...",
    [STR_DETECT_BY_IP] = "Визначити за IP",
    [STR_DETECTING] = "Визначення...",
    [STR_DETECTING_LOCATION] = "Визначення місцезнаходження...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Попереднє підключення ще не завершено",
    [STR_CANNOT_START_CONNECTION] = "Не вдалося розпочати підключення до мережі",
    [STR_CANNOT_START_LOOKUP] = "Не вдалося розпочати пошук місцезнаходження",
    [STR_SEARCH_FAILED] = "Помилка пошуку міста",
    [STR_LOCATION_NOT_DETECTED] = "Не вдалося визначити місто",
    [STR_ENTER_CITY] = "Введіть назву міста",
    [STR_CITY_TOO_LONG] = "Задовга назва міста",
    [STR_SELECT_CITY] = "Виберіть місто:",
    [STR_SELECT_IP_LOCATION] = "Приблизне місцезнаходження за IP: торкніться, щоб вибрати",
    [STR_SELECTED] = "Вибрано",
    [STR_IP_LOCATION] = "За IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Спочатку знайдіть і виберіть місто",
    [STR_SETTINGS_UNREADABLE] = "Не вдалося прочитати збережені налаштування. Виберіть місто і збережіть знову.",
    [STR_SETTINGS_BUSY] = "Не вдалося застосувати налаштування: триває запит погоди. Спробуйте трохи пізніше.",
    [STR_SETTINGS_SAVE_FAILED] = "Не вдалося зберегти налаштування. Залишено попередні.",
    [STR_SETTINGS_APPLY_FAILED] = "Налаштування збережено, але не застосовано. Перезапустіть застосунок.",
};

/* Polish */
static const char *const lang_pl[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Pogoda / Miasto",
    [STR_CITY] = "Miasto",
    [STR_SEARCHING] = "Wyszukiwanie...",
    [STR_DETECT_BY_IP] = "Wykryj po IP",
    [STR_DETECTING] = "Wykrywanie...",
    [STR_DETECTING_LOCATION] = "Wykrywanie lokalizacji...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Poprzednie połączenie jeszcze się nie zakończyło",
    [STR_CANNOT_START_CONNECTION] = "Nie można nawiązać połączenia z siecią",
    [STR_CANNOT_START_LOOKUP] = "Nie można rozpocząć wyszukiwania lokalizacji",
    [STR_SEARCH_FAILED] = "Błąd wyszukiwania miasta",
    [STR_LOCATION_NOT_DETECTED] = "Nie udało się wykryć miasta",
    [STR_ENTER_CITY] = "Wpisz nazwę miasta",
    [STR_CITY_TOO_LONG] = "Nazwa miasta jest za długa",
    [STR_SELECT_CITY] = "Wybierz miasto:",
    [STR_SELECT_IP_LOCATION] = "Przybliżona lokalizacja z IP: dotknij, aby wybrać",
    [STR_SELECTED] = "Wybrano",
    [STR_IP_LOCATION] = "Z IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Najpierw wyszukaj i wybierz miasto",
    [STR_SETTINGS_UNREADABLE] = "Nie można odczytać zapisanych ustawień. Wybierz miasto i zapisz ponownie.",
    [STR_SETTINGS_BUSY] = "Nie można zastosować ustawień: trwa pobieranie pogody. Spróbuj za chwilę.",
    [STR_SETTINGS_SAVE_FAILED] = "Nie można zapisać ustawień. Zachowano poprzednie.",
    [STR_SETTINGS_APPLY_FAILED] = "Ustawienia zapisano, ale nie zastosowano. Uruchom aplikację ponownie.",
};

/* German */
static const char *const lang_de[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Wetter / Stadt",
    [STR_CITY] = "Stadt",
    [STR_SEARCHING] = "Suche...",
    [STR_DETECT_BY_IP] = "Per IP ermitteln",
    [STR_DETECTING] = "Ermittlung...",
    [STR_DETECTING_LOCATION] = "Standort wird ermittelt...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Vorherige Netzwerkverbindung wird noch beendet",
    [STR_CANNOT_START_CONNECTION] = "Netzwerkverbindung kann nicht gestartet werden",
    [STR_CANNOT_START_LOOKUP] = "Standortsuche kann nicht gestartet werden",
    [STR_SEARCH_FAILED] = "Stadtsuche fehlgeschlagen",
    [STR_LOCATION_NOT_DETECTED] = "Stadt konnte nicht ermittelt werden",
    [STR_ENTER_CITY] = "Stadtnamen eingeben",
    [STR_CITY_TOO_LONG] = "Stadtname ist zu lang",
    [STR_SELECT_CITY] = "Stadt auswählen:",
    [STR_SELECT_IP_LOCATION] = "Ungefährer Standort per IP: zum Auswählen tippen",
    [STR_SELECTED] = "Ausgewählt",
    [STR_IP_LOCATION] = "Per IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Zuerst eine Stadt suchen und auswählen",
    [STR_SETTINGS_UNREADABLE] = "Gespeicherte Einstellungen können nicht gelesen werden. Stadt auswählen und erneut speichern.",
    [STR_SETTINGS_BUSY] = "Einstellungen können nicht übernommen werden: Wetterabfrage läuft. Bitte gleich erneut versuchen.",
    [STR_SETTINGS_SAVE_FAILED] = "Einstellungen können nicht gespeichert werden. Vorherige Einstellungen bleiben erhalten.",
    [STR_SETTINGS_APPLY_FAILED] = "Einstellungen gespeichert, aber nicht übernommen. App neu starten.",
};

/* French */
static const char *const lang_fr[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Météo / Ville",
    [STR_CITY] = "Ville",
    [STR_SEARCHING] = "Recherche...",
    [STR_DETECT_BY_IP] = "Détecter par IP",
    [STR_DETECTING] = "Détection...",
    [STR_DETECTING_LOCATION] = "Détection de la position...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "La connexion réseau précédente n'est pas terminée",
    [STR_CANNOT_START_CONNECTION] = "Impossible de lancer la connexion réseau",
    [STR_CANNOT_START_LOOKUP] = "Impossible de lancer la recherche de position",
    [STR_SEARCH_FAILED] = "Échec de la recherche de ville",
    [STR_LOCATION_NOT_DETECTED] = "Impossible de détecter la ville",
    [STR_ENTER_CITY] = "Saisissez le nom d'une ville",
    [STR_CITY_TOO_LONG] = "Nom de ville trop long",
    [STR_SELECT_CITY] = "Choisissez une ville :",
    [STR_SELECT_IP_LOCATION] = "Position approximative par IP : touchez pour choisir",
    [STR_SELECTED] = "Sélectionné",
    [STR_IP_LOCATION] = "Par IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Recherchez et choisissez d'abord une ville",
    [STR_SETTINGS_UNREADABLE] = "Impossible de lire les paramètres enregistrés. Choisissez une ville et enregistrez à nouveau.",
    [STR_SETTINGS_BUSY] = "Impossible d'appliquer les paramètres : requête météo en cours. Réessayez dans un instant.",
    [STR_SETTINGS_SAVE_FAILED] = "Impossible d'enregistrer les paramètres. Les précédents sont conservés.",
    [STR_SETTINGS_APPLY_FAILED] = "Paramètres enregistrés mais non appliqués. Redémarrez l'application.",
};

/* Spanish */
static const char *const lang_es[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Tiempo / Ciudad",
    [STR_CITY] = "Ciudad",
    [STR_SEARCHING] = "Buscando...",
    [STR_DETECT_BY_IP] = "Detectar por IP",
    [STR_DETECTING] = "Detectando...",
    [STR_DETECTING_LOCATION] = "Detectando ubicación...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "La conexión de red anterior aún no ha terminado",
    [STR_CANNOT_START_CONNECTION] = "No se puede iniciar la conexión de red",
    [STR_CANNOT_START_LOOKUP] = "No se puede iniciar la búsqueda de ubicación",
    [STR_SEARCH_FAILED] = "Error al buscar la ciudad",
    [STR_LOCATION_NOT_DETECTED] = "No se pudo detectar la ciudad",
    [STR_ENTER_CITY] = "Introduce el nombre de una ciudad",
    [STR_CITY_TOO_LONG] = "El nombre de la ciudad es demasiado largo",
    [STR_SELECT_CITY] = "Selecciona una ciudad:",
    [STR_SELECT_IP_LOCATION] = "Ubicación aproximada por IP: toca para seleccionar",
    [STR_SELECTED] = "Seleccionado",
    [STR_IP_LOCATION] = "Por IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Primero busca y selecciona una ciudad",
    [STR_SETTINGS_UNREADABLE] = "No se pueden leer los ajustes guardados. Selecciona una ciudad y guarda de nuevo.",
    [STR_SETTINGS_BUSY] = "No se pueden aplicar los ajustes: hay una consulta del tiempo en curso. Inténtalo en un momento.",
    [STR_SETTINGS_SAVE_FAILED] = "No se pueden guardar los ajustes. Se mantienen los anteriores.",
    [STR_SETTINGS_APPLY_FAILED] = "Ajustes guardados, pero no aplicados. Reinicia la aplicación.",
};

/* Italian */
static const char *const lang_it[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Meteo / Città",
    [STR_CITY] = "Città",
    [STR_SEARCHING] = "Ricerca...",
    [STR_DETECT_BY_IP] = "Rileva da IP",
    [STR_DETECTING] = "Rilevamento...",
    [STR_DETECTING_LOCATION] = "Rilevamento posizione...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "La connessione di rete precedente non è ancora terminata",
    [STR_CANNOT_START_CONNECTION] = "Impossibile avviare la connessione di rete",
    [STR_CANNOT_START_LOOKUP] = "Impossibile avviare la ricerca della posizione",
    [STR_SEARCH_FAILED] = "Ricerca della città non riuscita",
    [STR_LOCATION_NOT_DETECTED] = "Impossibile rilevare la città",
    [STR_ENTER_CITY] = "Inserisci il nome di una città",
    [STR_CITY_TOO_LONG] = "Nome della città troppo lungo",
    [STR_SELECT_CITY] = "Seleziona una città:",
    [STR_SELECT_IP_LOCATION] = "Posizione approssimativa da IP: tocca per selezionare",
    [STR_SELECTED] = "Selezionato",
    [STR_IP_LOCATION] = "Da IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Prima cerca e seleziona una città",
    [STR_SETTINGS_UNREADABLE] = "Impossibile leggere le impostazioni salvate. Seleziona una città e salva di nuovo.",
    [STR_SETTINGS_BUSY] = "Impossibile applicare le impostazioni: richiesta meteo in corso. Riprova tra poco.",
    [STR_SETTINGS_SAVE_FAILED] = "Impossibile salvare le impostazioni. Mantenute quelle precedenti.",
    [STR_SETTINGS_APPLY_FAILED] = "Impostazioni salvate ma non applicate. Riavvia l'app.",
};

/* Dutch */
static const char *const lang_nl[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Weer / Stad",
    [STR_CITY] = "Stad",
    [STR_SEARCHING] = "Zoeken...",
    [STR_DETECT_BY_IP] = "Via IP bepalen",
    [STR_DETECTING] = "Bepalen...",
    [STR_DETECTING_LOCATION] = "Locatie bepalen...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Vorige netwerkverbinding wordt nog afgerond",
    [STR_CANNOT_START_CONNECTION] = "Kan netwerkverbinding niet starten",
    [STR_CANNOT_START_LOOKUP] = "Kan locatie zoeken niet starten",
    [STR_SEARCH_FAILED] = "Zoeken naar stad mislukt",
    [STR_LOCATION_NOT_DETECTED] = "Kan stad niet bepalen",
    [STR_ENTER_CITY] = "Voer een plaatsnaam in",
    [STR_CITY_TOO_LONG] = "Plaatsnaam is te lang",
    [STR_SELECT_CITY] = "Kies een stad:",
    [STR_SELECT_IP_LOCATION] = "Geschatte locatie via IP: tik om te kiezen",
    [STR_SELECTED] = "Gekozen",
    [STR_IP_LOCATION] = "Via IP",
    [STR_SEARCH_AND_SELECT_FIRST] = "Zoek en kies eerst een stad",
    [STR_SETTINGS_UNREADABLE] = "Kan opgeslagen instellingen niet lezen. Kies een stad en sla opnieuw op.",
    [STR_SETTINGS_BUSY] = "Kan instellingen niet toepassen: weergegevens worden opgehaald. Probeer het zo opnieuw.",
    [STR_SETTINGS_SAVE_FAILED] = "Kan instellingen niet opslaan. Vorige instellingen behouden.",
    [STR_SETTINGS_APPLY_FAILED] = "Instellingen opgeslagen maar niet toegepast. Start de app opnieuw.",
};

/* Turkish */
static const char *const lang_tr[STR_COUNT] = {
    [STR_WEATHER_CITY] = "Hava / Şehir",
    [STR_CITY] = "Şehir",
    [STR_SEARCHING] = "Aranıyor...",
    [STR_DETECT_BY_IP] = "IP ile bul",
    [STR_DETECTING] = "Belirleniyor...",
    [STR_DETECTING_LOCATION] = "Konum belirleniyor...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "Önceki ağ bağlantısı henüz tamamlanmadı",
    [STR_CANNOT_START_CONNECTION] = "Ağ bağlantısı başlatılamıyor",
    [STR_CANNOT_START_LOOKUP] = "Konum araması başlatılamıyor",
    [STR_SEARCH_FAILED] = "Şehir araması başarısız",
    [STR_LOCATION_NOT_DETECTED] = "Şehir belirlenemedi",
    [STR_ENTER_CITY] = "Bir şehir adı girin",
    [STR_CITY_TOO_LONG] = "Şehir adı çok uzun",
    [STR_SELECT_CITY] = "Bir şehir seçin:",
    [STR_SELECT_IP_LOCATION] = "IP'ye göre yaklaşık konum: seçmek için dokunun",
    [STR_SELECTED] = "Seçildi",
    [STR_IP_LOCATION] = "IP ile",
    [STR_SEARCH_AND_SELECT_FIRST] = "Önce bir şehir arayıp seçin",
    [STR_SETTINGS_UNREADABLE] = "Kayıtlı ayarlar okunamıyor. Bir şehir seçip yeniden kaydedin.",
    [STR_SETTINGS_BUSY] = "Ayarlar uygulanamıyor: hava durumu isteği sürüyor. Biraz sonra tekrar deneyin.",
    [STR_SETTINGS_SAVE_FAILED] = "Ayarlar kaydedilemiyor. Önceki ayarlar korundu.",
    [STR_SETTINGS_APPLY_FAILED] = "Ayarlar kaydedildi ancak uygulanamadı. Uygulamayı yeniden başlatın.",
};

/* Chinese (Simplified) */
static const char *const lang_zh[STR_COUNT] = {
    [STR_WEATHER_CITY] = "天气 / 城市",
    [STR_CITY] = "城市",
    [STR_SEARCHING] = "正在搜索...",
    [STR_DETECT_BY_IP] = "通过 IP 定位",
    [STR_DETECTING] = "正在定位...",
    [STR_DETECTING_LOCATION] = "正在确定位置...",
    [STR_PREVIOUS_CONNECTION_BUSY] = "上一个网络连接尚未结束",
    [STR_CANNOT_START_CONNECTION] = "无法启动网络连接",
    [STR_CANNOT_START_LOOKUP] = "无法启动位置查询",
    [STR_SEARCH_FAILED] = "城市搜索失败",
    [STR_LOCATION_NOT_DETECTED] = "无法确定城市",
    [STR_ENTER_CITY] = "请输入城市名称",
    [STR_CITY_TOO_LONG] = "城市名称过长",
    [STR_SELECT_CITY] = "请选择城市：",
    [STR_SELECT_IP_LOCATION] = "基于 IP 的大致位置：点击选择",
    [STR_SELECTED] = "已选择",
    [STR_IP_LOCATION] = "IP 定位",
    [STR_SEARCH_AND_SELECT_FIRST] = "请先搜索并选择城市",
    [STR_SETTINGS_UNREADABLE] = "无法读取已保存的设置。请选择城市并重新保存。",
    [STR_SETTINGS_BUSY] = "无法应用设置：天气请求正在进行。请稍后重试。",
    [STR_SETTINGS_SAVE_FAILED] = "无法保存设置。已保留之前的设置。",
    [STR_SETTINGS_APPLY_FAILED] = "设置已保存但未能应用。请重新启动应用。",
};

typedef struct
{
  const char *code;
  const char *const *texts;
} Language;

static const Language languages[] = {
    {"ru", lang_ru},
    {"be", lang_be},
    {"uk", lang_uk},
    {"pl", lang_pl},
    {"de", lang_de},
    {"fr", lang_fr},
    {"es", lang_es},
    {"it", lang_it},
    {"nl", lang_nl},
    {"tr", lang_tr},
    {"zh", lang_zh},
};

static const char *const *current_texts;

void i18n_set_language(const char *code)
{
  current_texts = NULL;
  if (code == NULL)
  {
    return;
  }

  /* Accept both "ru" and regional forms such as "pt_br" or "zh-cn". */
  const size_t length = strcspn(code, "_-");
  for (size_t i = 0; i < sizeof(languages) / sizeof(languages[0]); ++i)
  {
    if (strlen(languages[i].code) == length && strncmp(code, languages[i].code, length) == 0)
    {
      current_texts = languages[i].texts;
      return;
    }
  }
}

const char *tr(StringId id)
{
  if ((unsigned)id >= STR_COUNT || strings[id].english == NULL)
  {
    fprintf(stderr, "i18n: unknown string id %d\n", (int)id);
    return "?";
  }

  const StringEntry *entry = &strings[id];
  if (entry->system_key != NULL)
  {
    const char *text = GetLangText(entry->system_key);

    /* A missing key is returned unchanged (with or without '@'). */
    if (text != NULL && text[0] != '\0' && strcmp(text, entry->system_key) != 0 && strcmp(text, entry->system_key + 1) != 0)
    {
      return text;
    }
  }
  if (current_texts != NULL && current_texts[id] != NULL)
  {
    return current_texts[id];
  }
  return entry->english;
}
