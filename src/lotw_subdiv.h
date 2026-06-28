// AUTO-GENERATED from tqsl-2.8.6 config.xml. LoTW administrative subdivisions.
// Flash-resident const tables (ESP32-S3 maps const to flash; zero heap cost).
// Do not hand-edit; regenerate from config.xml if LoTW updates its enums.
//
// Layout: SubdivEntry{code,name}. Primary subdivisions are one array each;
// US counties are one array per state, indexed by US_COUNTY_INDEX (state-gated,
// so only a single state's county list is ever scanned).
#pragma once
#include <Arduino.h>

struct SubdivEntry { const char* code; const char* name; };

static const SubdivEntry SUB_US_STATE[] = {
  {"AL","Alabama"},{"AZ","Arizona"},{"AR","Arkansas"},{"CA","California"},{"CO","Colorado"},
  {"CT","Connecticut"},{"DE","Delaware"},{"DC","District of Columbia"},{"FL","Florida"},{"GA","Georgia"},
  {"ID","Idaho"},{"IL","Illinois"},{"IN","Indiana"},{"IA","Iowa"},{"KS","Kansas"},{"KY","Kentucky"},
  {"LA","Louisiana"},{"ME","Maine"},{"MD","Maryland"},{"MA","Massachusetts"},{"MI","Michigan"},
  {"MN","Minnesota"},{"MS","Mississippi"},{"MO","Missouri"},{"MT","Montana"},{"NE","Nebraska"},
  {"NV","Nevada"},{"NH","New Hampshire"},{"NJ","New Jersey"},{"NM","New Mexico"},{"NY","New York"},
  {"NC","North Carolina"},{"ND","North Dakota"},{"OH","Ohio"},{"OK","Oklahoma"},{"OR","Oregon"},
  {"PA","Pennsylvania"},{"RI","Rhode Island"},{"SC","South Carolina"},{"SD","South Dakota"},
  {"TN","Tennessee"},{"TX","Texas"},{"UT","Utah"},{"VT","Vermont"},{"VA","Virginia"},{"WA","Washington"},
  {"WV","West Virginia"},{"WI","Wisconsin"},{"WY","Wyoming"},{"AK","Alaska"},{"HI","Hawaii"},
};
static const int SUB_US_STATE_N = 51;

static const SubdivEntry SUB_CA_PROVINCE[] = {
  {"AB","Alberta"},{"BC","British Columbia"},{"MB","Manitoba"},{"NB","New Brunswick"},
  {"NF","Newfoundland and Labrador"},{"NT","Northwest Territories"},{"NS","Nova Scotia"},{"NU","Nunavut"},
  {"ON","Ontario"},{"PE","Prince Edward Island"},{"PQ","Quebec"},{"SK","Saskatchewan"},
  {"YT","Yukon Territory"},
};
static const int SUB_CA_PROVINCE_N = 13;

static const SubdivEntry SUB_RU_OBLAST[] = {
  {"KA","Kaliningradskaya oblast&apos; (Kaliningrad Oblast)"},
  {"AB","Aginsky Buryatsky avtonomny okrug (Agin-Buryat Autonomous Okrug) [DELETED]"},
  {"AL","Altayskiy kray (Altai Krai)"},{"AM","Amurskaya oblast&apos; (Amur Oblast)"},
  {"BA","Respublika Bashkortostan (Republic of Bashkortostan) (Bashkortostan Republic)"},
  {"BU","Respublika Buryatiya (Republic of Buryatia)"},
  {"CB","Chelyabinskaya oblast&apos; (Chelyabinsk Oblast)"},
  {"CK","Chukotsky avtonomny okrug (Chukotka Autonomous Okrug)"},
  {"CT","Zabaykalsky kray (Zabaykalsky Krai)"},
  {"EA","Yevreyskaya avtonomnaya oblast&apos; (Jewish Autonomous Oblast)"},
  {"EW","Evenkiysky avtonomny okrug (Evenk Autonomous Okrug) [DELETED]"},
  {"GA","Respublika Altay (Altai Republic) (Altay Republits)"},
  {"HA","Respublika Khakasiya (Khakasiya Republits) (Republic of Khakassia)"},
  {"HK","Khabarovskiy kray (Khabarovsk Krai)"},
  {"HM","Khanty-Mansiyskiy avtonomnyy okrug (Khanty-Mansi Autonomous Okrug)"},
  {"IR","Irkutskaya oblast&apos; (Irkutsk Oblast)"},
  {"JN","Yamalo-Nenetsky avtonomny okrug (Yamalo-Nenets Autonomous Okrug)"},
  {"KE","Kemerovskaya oblast&apos; (Kemerovo Oblast)"},
  {"KJ","Koryaksky avtonomny okrug (Koryak Autonomous Okrug) [DELETED]"},
  {"KK","Krasnoyarskiy kray (Krasnoyarsk Krai)"},{"KN","Kurganskaya oblast&apos; (Kurgan Oblast)"},
  {"KT","Kamchatskiy kray (Kamchatka Krai)"},{"MG","Magadanskaya oblast&apos; (Magadan Oblast)"},
  {"NS","Novosibirskaya oblast&apos; (Novosibirsk Oblast)"},
  {"OB","Orenburgskaya oblast&apos; (Orenburg Oblast)"},{"OM","Omskaya oblast&apos; (Omsk Oblast)"},
  {"PK","Primorskiy kray (Primorsky Krai)"},{"SL","Sakhalinskaya oblast&apos; (Sakhalin Oblast)"},
  {"SV","Sverdlovskaya oblast&apos; (Sverdlovsk Oblast)"},
  {"TM","Taymyrsky Dolgano-Nenetsky avtonomny okru (Taymyr Autonomous Okrug) (Taymyria) [DELETED]"},
  {"TN","Tyumenskaya oblast&apos; (Tyumen Oblast)"},{"TO","Tomskaya oblast&apos; (Tomsk Oblast)"},
  {"TU","Respublika Tyva (Tyva Republic) (Tyva Republits)"},
  {"UO","Ust-Ordynsky avtonomny okrug (Ust-Orda Buryat Autonomous Okrug) [DELETED]"},
  {"YA","Respublika Sakha (Yakutiya)"},{"LO","Leningradskaya oblast&apos; (Leningrad Oblast)"},
  {"AD","Adygea (Adygea Republic}"},{"AO","Astrakhanskaya oblast&apos; (Astrakhan Oblast)"},
  {"AR","Arkhangelskaya oblast&apos; (Arkhangelsk Oblast)"},
  {"BO","Belgorodskaya oblast&apos; (Belgorod Oblast)"},{"BR","Bryanskaya oblast&apos; (Bryansk Oblast)"},
  {"CC","Chechenskaya Respublika (Chechnya)"},{"CU","Chuvashiya (Chuvashia Republic) (Chuvash Republic)"},
  {"DA","Dagestan Republits (Dagestan Republic)"},{"IN","Respublika Ingushetiya (Republic of Ingushetia)"},
  {"IV","Ivanovskaya oblast&apos; (Ivanovo Oblast)"},{"JA","Yaroslavskaya oblast&apos; (Yaroslavl Oblast)"},
  {"KB","Kabardino-Balkarskaya Respublika (Kabardino-Balkaria)"},
  {"KC","Karachay-Cherkessia (Karachay-Cherkess Republic)"},
  {"KG","Kaluzhskaya oblast&apos; (Kaluga Oblast)"},{"KI","Kirovskaya oblast&apos; (Kirov Oblast)"},
  {"KL","Respublika Karelia (Karelia Republic)"},
  {"KM","Respublika Kalmykiya (Kalmykia Republic) (Kalmykia)"},{"KO","Respúblika Komi (Komi Republic)"},
  {"KP","Komi-Permyatsky avtonomny okrug (Komi-Permyak Autonomous Okrug) [DELETED]"},
  {"KR","Krasnodarskiy kray (Krasnodar Krai)"},{"KS","Kostromskaya oblast&apos; (Kostroma Oblast)"},
  {"KU","Kurskaya oblast&apos; (Kursk Oblast)"},{"LO","Leningradskaya oblast&apos; (Leningrad Oblast)"},
  {"LP","Lipetsk Oblast (Lipetskaya oblast&apos;)"},{"MA","Moskva (Moscow)"},
  {"MD","Respublika Mordoviya (Republic of Mordovia)"},{"MO","Moskovskaya oblast&apos; (Moscow Oblast)"},
  {"MR","Respublika Mariy El (Mari El Republic)"},{"MU","Murmanskaya oblast&apos; (Murmansk Oblast)"},
  {"NN","Nizhegorodskaya oblast&apos; (Nizhny Novgorod Oblast)"},
  {"NO","Nenetsky avtonomny okrug (Nenets Autonomous Okrug)"},
  {"NV","Novgorodskaya oblast&apos; (Novgorod Oblast)"},{"OR","Orlovskaya oblast&apos; (Oryol Oblast)"},
  {"PE","Penzenskaya oblast&apos; (Penza Oblast)"},{"PM","Permskiy kray	 (Perm Krai)"},
  {"PS","Pskovskaya oblast&apos; (Pskov Oblast)"},{"RA","Ryazanskaya oblast&apos; (Ryazan Oblast)"},
  {"RO","Rostovskaya oblast&apos; (Rostov Oblast)"},{"SA","Saratovskaya oblast&apos; (Saratov Oblast)"},
  {"SM","Smolenskaya oblast&apos; (Smolensk Oblast)"},
  {"SO","Severnaya Osetiya (North Ossetia-Alania) (North Ossetia-Alania Republic)"},
  {"SP","Sankt-Peterburg (Saint Petersburg)"},{"SR","Samarskaya oblast&apos; (Samara Oblast)"},
  {"ST","Stavropolskiy kray (Stavropol Krai)"},
  {"TA","Respublika Tatarstan (Tatarstan) (Tatarstan Republic)"},
  {"TB","Tambovskaya oblast&apos; (Tambov Oblast)"},{"TL","Tulskaya oblast&apos; (Tula Oblast)"},
  {"TV","Tverskaya oblast&apos;  (Tver Oblast)"},{"UD","Udmurtskaja Respublika (Udmurt Republic)"},
  {"UL","Ulyanovskaya oblast&apos; (Ulyanovsk Oblast)"},
  {"VG","Volgogradskaya oblast&apos; (Volgograd Oblast)"},
  {"VL","Vladimirskaya oblast&apos; (Vladimir Oblast)"},{"VO","Vologodskaya oblast&apos; (Vologda Oblast)"},
  {"VR","Voronezhskaya oblast&apos; (Voronezh Oblast)"},
  {"AR","Arkhangelskaya oblast&apos; (Arkhangelsk Oblast)"},
};
static const int SUB_RU_OBLAST_N = 91;

static const SubdivEntry SUB_JA_PREFECTURE[] = {
  {"01","Hokkaido-do"},{"02","Aomori-ken"},{"03","Iwate-ken"},{"04","Akita-ken"},{"05","Yamagata-ken"},
  {"06","Miyagi-ken"},{"07","Fukushima-ken"},{"08","Niigata-ken"},{"09","Nagano-ken"},{"10","Tokyo-to"},
  {"11","Kanagawa-ken"},{"12","Chiba-ken"},{"13","Saitama-ken"},{"14","Ibaraki-ken"},{"15","Tochigi-ken"},
  {"16","Gunma-ken"},{"17","Yamanashi-ken"},{"18","Shizuoka-ken"},{"19","Gifu-ken"},{"20","Aichi-ken"},
  {"21","Mie-ken"},{"22","Kyoto-ken"},{"23","Shiga-ken"},{"24","Nara-ken"},{"25","Osaka-fu"},
  {"26","Wakayama-ken"},{"27","Hyogo-ken"},{"28","Toyama-ken"},{"29","Fukui-ken"},{"30","Ishikawa-ken"},
  {"31","Okayama-ken"},{"32","Shimane-ken"},{"33","Yamaguchi-ken"},{"34","Tottori-ken"},
  {"35","Hiroshima-ken"},{"36","Kagawa-ken"},{"37","Tokushima-ken"},{"38","Ehime-ken"},{"39","Kochi-ken"},
  {"40","Fukuoka-ken"},{"41","Saga-ken"},{"42","Nagasaki-ken"},{"43","Kumamoto-ken"},{"44","Oita-ken"},
  {"45","Miyazaki-ken"},{"46","Kagoshima-ken"},{"47","Okinawa-ken"},
};
static const int SUB_JA_PREFECTURE_N = 47;

static const SubdivEntry SUB_CN_PROVINCE[] = {
  {"AH","Anhui"},{"BJ","Beijing"},{"CQ","Chongqing"},{"FJ","Fujian"},{"GD","Guangdong"},{"GS","Gansu"},
  {"GX","Guangxi"},{"GZ","Guizhou"},{"HA","Henan"},{"HB","Hubei"},{"HE","Hebei"},{"HI","Hainan"},
  {"HL","Heilongjiang"},{"HN","Hunan"},{"JL","Jilin"},{"JS","Jiangsu"},{"JX","Jiangxi"},{"LN","Liaoning"},
  {"NM","Nei Mongol"},{"NX","Ningxia"},{"QH","Qinghai"},{"SC","Sichuan"},{"SD","Shandong"},
  {"SH","Shanghai"},{"SN","Shaanxi"},{"SX","Shanxi"},{"TJ","Tianjin"},{"XJ","Xinjiang"},{"XZ","Xizang"},
  {"YN","Yunnan"},{"ZJ","Zhejiang"},
};
static const int SUB_CN_PROVINCE_N = 31;

static const SubdivEntry SUB_AU_STATE[] = {
  {"ACT","Australian Capital Territory"},{"NSW","New South Wales"},{"VIC","Victoria"},{"QLD","Queensland"},
  {"SA","South Australia"},{"WA","Western Australia"},{"TAS","Tasmania"},{"NT","Northern Territory"},
};
static const int SUB_AU_STATE_N = 8;

static const SubdivEntry SUB_FI_KUNTA[] = {
  {"6","Hammarland"},{"1","Brändö"},{"2","Eckerö"},{"3","Finström"},{"4","Föglö"},{"5","Geta"},
  {"6","Hammarland"},{"7","Jomala"},{"8","Kumlinge"},{"9","Kökar"},{"10","Lemland"},{"11","Lumparland"},
  {"12","Maarianhamina (Mariehamn)"},{"13","Saltvik"},{"14","Sottunga"},{"15","Sund"},{"16","Vårdö"},
  {"51","Märket Reef"},
};
static const int SUB_FI_KUNTA_N = 18;

static const SubdivEntry CNTY_AK[] = {
  {"Aleutians East","Aleutians East Borough"},{"Aleutians West","Aleutians West Census Area"},
  {"Anchorage","Anchorage Municipality"},{"Bethel","Bethel Census Area"},
  {"Bristol Bay","Bristol Bay Borough"},{"Denali","Denali Borough"},{"Dillingham","Dillingham Census Area"},
  {"Fairbanks North Star","Fairbanks North Star Borough"},{"Haines","Haines Borough"},
  {"Hoonah-Angoon","Hoonah-Angoon Census Area"},{"Juneau","Juneau City and Borough"},
  {"Kenai Peninsula","Kenai Peninsula Borough"},{"Ketchikan Gateway","Ketchikan Gateway Borough"},
  {"Kodiak Island","Kodiak Island Borough"},{"Lake and Peninsula","Lake and Peninsula Borough"},
  {"Matanuska Susitna","Matanuska-Susitna Borough"},{"Nome","Nome Census Area"},
  {"North Slope","North Slope Borough"},{"Northwest Arctic","Northwest Arctic Borough"},
  {"Petersburg","Petersburg Census Area"},{"Prince of Wales-Hyder","Prince of Wales-Hyder Census Area"},
  {"Sitka","Sitka City and Borough"},{"Skagway","Skagway Municipality"},
  {"Southeast Fairbanks","Southeast Fairbanks Census Area"},{"Valdez Cordova","Valdez-Cordova Census Area"},
  {"Wade Hampton","Wade Hampton Census Area"},{"Wrangell","Wrangell City and Borough"},
  {"Yakutat","Yakutat City and Borough"},{"Yukon Koyukuk","Yukon-Koyukuk Census Area"},
  {"Prince Wales Ketchikan","Prince of Wales-Outer-Ketichikan Census Area"},
  {"Skagway Hoonah Angoon","Skagway-Hoonah-Angoon Census Area"},
};
static const int CNTY_AK_N = 31;

static const SubdivEntry CNTY_AL[] = {
  {"Autauga","Autauga"},{"Baldwin","Baldwin"},{"Barbour","Barbour"},{"Bibb","Bibb"},{"Blount","Blount"},
  {"Bullock","Bullock"},{"Butler","Butler"},{"Calhoun","Calhoun"},{"Chambers","Chambers"},
  {"Cherokee","Cherokee"},{"Chilton","Chilton"},{"Choctaw","Choctaw"},{"Clarke","Clarke"},{"Clay","Clay"},
  {"Cleburne","Cleburne"},{"Coffee","Coffee"},{"Colbert","Colbert"},{"Conecuh","Conecuh"},{"Coosa","Coosa"},
  {"Covington","Covington"},{"Crenshaw","Crenshaw"},{"Cullman","Cullman"},{"Dale","Dale"},
  {"Dallas","Dallas"},{"De Kalb","DeKalb"},{"Elmore","Elmore"},{"Escambia","Escambia"},{"Etowah","Etowah"},
  {"Fayette","Fayette"},{"Franklin","Franklin"},{"Geneva","Geneva"},{"Greene","Greene"},{"Hale","Hale"},
  {"Henry","Henry"},{"Houston","Houston"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Lamar","Lamar"},
  {"Lauderdale","Lauderdale"},{"Lawrence","Lawrence"},{"Lee","Lee"},{"Limestone","Limestone"},
  {"Lowndes","Lowndes"},{"Macon","Macon"},{"Madison","Madison"},{"Marengo","Marengo"},{"Marion","Marion"},
  {"Marshall","Marshall"},{"Mobile","Mobile"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},
  {"Morgan","Morgan"},{"Perry","Perry"},{"Pickens","Pickens"},{"Pike","Pike"},{"Randolph","Randolph"},
  {"Russell","Russell"},{"Saint Clair","St. Clair"},{"Shelby","Shelby"},{"Sumter","Sumter"},
  {"Talladega","Talladega"},{"Tallapoosa","Tallapoosa"},{"Tuscaloosa","Tuscaloosa"},{"Walker","Walker"},
  {"Washington","Washington"},{"Wilcox","Wilcox"},{"Winston","Winston"},
};
static const int CNTY_AL_N = 67;

static const SubdivEntry CNTY_AR[] = {
  {"Arkansas","Arkansas"},{"Ashley","Ashley"},{"Baxter","Baxter"},{"Benton","Benton"},{"Boone","Boone"},
  {"Bradley","Bradley"},{"Calhoun","Calhoun"},{"Carroll","Carroll"},{"Chicot","Chicot"},{"Clark","Clark"},
  {"Clay","Clay"},{"Cleburne","Cleburne"},{"Cleveland","Cleveland"},{"Columbia","Columbia"},
  {"Conway","Conway"},{"Craighead","Craighead"},{"Crawford","Crawford"},{"Crittenden","Crittenden"},
  {"Cross","Cross"},{"Dallas","Dallas"},{"Desha","Desha"},{"Drew","Drew"},{"Faulkner","Faulkner"},
  {"Franklin","Franklin"},{"Fulton","Fulton"},{"Garland","Garland"},{"Grant","Grant"},{"Greene","Greene"},
  {"Hempstead","Hempstead"},{"Hot Spring","Hot Spring"},{"Howard","Howard"},{"Independence","Independence"},
  {"Izard","Izard"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Johnson","Johnson"},
  {"Lafayette","Lafayette"},{"Lawrence","Lawrence"},{"Lee","Lee"},{"Lincoln","Lincoln"},
  {"Little River","Little River"},{"Logan","Logan"},{"Lonoke","Lonoke"},{"Madison","Madison"},
  {"Marion","Marion"},{"Miller","Miller"},{"Mississippi","Mississippi"},{"Monroe","Monroe"},
  {"Montgomery","Montgomery"},{"Nevada","Nevada"},{"Newton","Newton"},{"Ouachita","Ouachita"},
  {"Perry","Perry"},{"Phillips","Phillips"},{"Pike","Pike"},{"Poinsett","Poinsett"},{"Polk","Polk"},
  {"Pope","Pope"},{"Prairie","Prairie"},{"Pulaski","Pulaski"},{"Randolph","Randolph"},
  {"Saint Francis","St. Francis"},{"Saline","Saline"},{"Scott","Scott"},{"Searcy","Searcy"},
  {"Sebastian","Sebastian"},{"Sevier","Sevier"},{"Sharp","Sharp"},{"Stone","Stone"},{"Union","Union"},
  {"Van Buren","Van Buren"},{"Washington","Washington"},{"White","White"},{"Woodruff","Woodruff"},
  {"Yell","Yell"},
};
static const int CNTY_AR_N = 75;

static const SubdivEntry CNTY_AZ[] = {
  {"Apache","Apache"},{"Cochise","Cochise"},{"Coconino","Coconino"},{"Gila","Gila"},{"Graham","Graham"},
  {"Greenlee","Greenlee"},{"La Paz","La Paz"},{"Maricopa","Maricopa"},{"Mohave","Mohave"},
  {"Navajo","Navajo"},{"Pima","Pima"},{"Pinal","Pinal"},{"Santa Cruz","Santa Cruz"},{"Yavapai","Yavapai"},
  {"Yuma","Yuma"},
};
static const int CNTY_AZ_N = 15;

static const SubdivEntry CNTY_CA[] = {
  {"Alameda","Alameda"},{"Alpine","Alpine"},{"Amador","Amador"},{"Butte","Butte"},{"Calaveras","Calaveras"},
  {"Colusa","Colusa"},{"Contra Costa","Contra Costa"},{"Del Norte","Del Norte"},{"El Dorado","El Dorado"},
  {"Fresno","Fresno"},{"Glenn","Glenn"},{"Humboldt","Humboldt"},{"Imperial","Imperial"},{"Inyo","Inyo"},
  {"Kern","Kern"},{"Kings","Kings"},{"Lake","Lake"},{"Lassen","Lassen"},{"Los Angeles","Los Angeles"},
  {"Madera","Madera"},{"Marin","Marin"},{"Mariposa","Mariposa"},{"Mendocino","Mendocino"},
  {"Merced","Merced"},{"Modoc","Modoc"},{"Mono","Mono"},{"Monterey","Monterey"},{"Napa","Napa"},
  {"Nevada","Nevada"},{"Orange","Orange"},{"Placer","Placer"},{"Plumas","Plumas"},{"Riverside","Riverside"},
  {"Sacramento","Sacramento"},{"San Benito","San Benito"},{"San Bernardino","San Bernardino"},
  {"San Diego","San Diego"},{"San Francisco","San Francisco"},{"San Joaquin","San Joaquin"},
  {"San Luis Obispo","San Luis Obispo"},{"San Mateo","San Mateo"},{"Santa Barbara","Santa Barbara"},
  {"Santa Clara","Santa Clara"},{"Santa Cruz","Santa Cruz"},{"Shasta","Shasta"},{"Sierra","Sierra"},
  {"Siskiyou","Siskiyou"},{"Solano","Solano"},{"Sonoma","Sonoma"},{"Stanislaus","Stanislaus"},
  {"Sutter","Sutter"},{"Tehama","Tehama"},{"Trinity","Trinity"},{"Tulare","Tulare"},{"Tuolumne","Tuolumne"},
  {"Ventura","Ventura"},{"Yolo","Yolo"},{"Yuba","Yuba"},
};
static const int CNTY_CA_N = 58;

static const SubdivEntry CNTY_CO[] = {
  {"Adams","Adams"},{"Alamosa","Alamosa"},{"Arapahoe","Arapahoe"},{"Archuleta","Archuleta"},{"Baca","Baca"},
  {"Bent","Bent"},{"Boulder","Boulder"},{"Broomfield","Broomfield"},{"Chaffee","Chaffee"},
  {"Cheyenne","Cheyenne"},{"Clear Creek","Clear Creek"},{"Conejos","Conejos"},{"Costilla","Costilla"},
  {"Crowley","Crowley"},{"Custer","Custer"},{"Delta","Delta"},{"Denver","Denver"},{"Dolores","Dolores"},
  {"Douglas","Douglas"},{"Eagle","Eagle"},{"El Paso","El Paso"},{"Elbert","Elbert"},{"Fremont","Fremont"},
  {"Garfield","Garfield"},{"Gilpin","Gilpin"},{"Grand","Grand"},{"Gunnison","Gunnison"},
  {"Hinsdale","Hinsdale"},{"Huerfano","Huerfano"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},
  {"Kiowa","Kiowa"},{"Kit Carson","Kit Carson"},{"La Plata","La Plata"},{"Lake","Lake"},
  {"Larimer","Larimer"},{"Las Animas","Las Animas"},{"Lincoln","Lincoln"},{"Logan","Logan"},{"Mesa","Mesa"},
  {"Mineral","Mineral"},{"Moffat","Moffat"},{"Montezuma","Montezuma"},{"Montrose","Montrose"},
  {"Morgan","Morgan"},{"Otero","Otero"},{"Ouray","Ouray"},{"Park","Park"},{"Phillips","Phillips"},
  {"Pitkin","Pitkin"},{"Prowers","Prowers"},{"Pueblo","Pueblo"},{"Rio Blanco","Rio Blanco"},
  {"Rio Grande","Rio Grande"},{"Routt","Routt"},{"Saguache","Saguache"},{"San Juan","San Juan"},
  {"San Miguel","San Miguel"},{"Sedgwick","Sedgwick"},{"Summit","Summit"},{"Teller","Teller"},
  {"Washington","Washington"},{"Weld","Weld"},{"Yuma","Yuma"},
};
static const int CNTY_CO_N = 64;

static const SubdivEntry CNTY_CT[] = {
  {"Fairfield","Fairfield"},{"Hartford","Hartford"},{"Litchfield","Litchfield"},{"Middlesex","Middlesex"},
  {"New Haven","New Haven"},{"New London","New London"},{"Tolland","Tolland"},{"Windham","Windham"},
};
static const int CNTY_CT_N = 8;

static const SubdivEntry CNTY_DE[] = {
  {"Kent","Kent"},{"New Castle","New Castle"},{"Sussex","Sussex"},
};
static const int CNTY_DE_N = 3;

static const SubdivEntry CNTY_FL[] = {
  {"Alachua","Alachua"},{"Baker","Baker"},{"Bay","Bay"},{"Bradford","Bradford"},{"Brevard","Brevard"},
  {"Broward","Broward"},{"Calhoun","Calhoun"},{"Charlotte","Charlotte"},{"Citrus","Citrus"},{"Clay","Clay"},
  {"Collier","Collier"},{"Columbia","Columbia"},{"De Soto","DeSoto"},{"Dixie","Dixie"},{"Duval","Duval"},
  {"Escambia","Escambia"},{"Flagler","Flagler"},{"Franklin","Franklin"},{"Gadsden","Gadsden"},
  {"Gilchrist","Gilchrist"},{"Glades","Glades"},{"Gulf","Gulf"},{"Hamilton","Hamilton"},{"Hardee","Hardee"},
  {"Hendry","Hendry"},{"Hernando","Hernando"},{"Highlands","Highlands"},{"Hillsborough","Hillsborough"},
  {"Holmes","Holmes"},{"Indian River","Indian River"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},
  {"Lafayette","Lafayette"},{"Lake","Lake"},{"Lee","Lee"},{"Leon","Leon"},{"Levy","Levy"},
  {"Liberty","Liberty"},{"Madison","Madison"},{"Manatee","Manatee"},{"Marion","Marion"},{"Martin","Martin"},
  {"Miami-Dade","Miami-Dade"},{"Monroe","Monroe"},{"Nassau","Nassau"},{"Okaloosa","Okaloosa"},
  {"Okeechobee","Okeechobee"},{"Orange","Orange"},{"Osceola","Osceola"},{"Palm Beach","Palm Beach"},
  {"Pasco","Pasco"},{"Pinellas","Pinellas"},{"Polk","Polk"},{"Putnam","Putnam"},{"Saint Johns","St. Johns"},
  {"Saint Lucie","St. Lucie"},{"Santa Rosa","Santa Rosa"},{"Sarasota","Sarasota"},{"Seminole","Seminole"},
  {"Sumter","Sumter"},{"Suwannee","Suwannee"},{"Taylor","Taylor"},{"Union","Union"},{"Volusia","Volusia"},
  {"Wakulla","Wakulla"},{"Walton","Walton"},{"Washington","Washington"},
};
static const int CNTY_FL_N = 67;

static const SubdivEntry CNTY_GA[] = {
  {"Appling","Appling"},{"Atkinson","Atkinson"},{"Bacon","Bacon"},{"Baker","Baker"},{"Baldwin","Baldwin"},
  {"Banks","Banks"},{"Barrow","Barrow"},{"Bartow","Bartow"},{"Ben Hill","Ben Hill"},{"Berrien","Berrien"},
  {"Bibb","Bibb"},{"Bleckley","Bleckley"},{"Brantley","Brantley"},{"Brooks","Brooks"},{"Bryan","Bryan"},
  {"Bulloch","Bulloch"},{"Burke","Burke"},{"Butts","Butts"},{"Calhoun","Calhoun"},{"Camden","Camden"},
  {"Candler","Candler"},{"Carroll","Carroll"},{"Catoosa","Catoosa"},{"Charlton","Charlton"},
  {"Chatham","Chatham"},{"Chattahoochee","Chattahoochee"},{"Chattooga","Chattooga"},{"Cherokee","Cherokee"},
  {"Clarke","Clarke"},{"Clay","Clay"},{"Clayton","Clayton"},{"Clinch","Clinch"},{"Cobb","Cobb"},
  {"Coffee","Coffee"},{"Colquitt","Colquitt"},{"Columbia","Columbia"},{"Cook","Cook"},{"Coweta","Coweta"},
  {"Crawford","Crawford"},{"Crisp","Crisp"},{"Dade","Dade"},{"Dawson","Dawson"},{"Decatur","Decatur"},
  {"Dekalb","DeKalb"},{"Dodge","Dodge"},{"Dooly","Dooly"},{"Dougherty","Dougherty"},{"Douglas","Douglas"},
  {"Early","Early"},{"Echols","Echols"},{"Effingham","Effingham"},{"Elbert","Elbert"},{"Emanuel","Emanuel"},
  {"Evans","Evans"},{"Fannin","Fannin"},{"Fayette","Fayette"},{"Floyd","Floyd"},{"Forsyth","Forsyth"},
  {"Franklin","Franklin"},{"Fulton","Fulton"},{"Gilmer","Gilmer"},{"Glascock","Glascock"},{"Glynn","Glynn"},
  {"Gordon","Gordon"},{"Grady","Grady"},{"Greene","Greene"},{"Gwinnett","Gwinnett"},
  {"Habersham","Habersham"},{"Hall","Hall"},{"Hancock","Hancock"},{"Haralson","Haralson"},
  {"Harris","Harris"},{"Hart","Hart"},{"Heard","Heard"},{"Henry","Henry"},{"Houston","Houston"},
  {"Irwin","Irwin"},{"Jackson","Jackson"},{"Jasper","Jasper"},{"Jeff Davis","Jeff Davis"},
  {"Jefferson","Jefferson"},{"Jenkins","Jenkins"},{"Johnson","Johnson"},{"Jones","Jones"},{"Lamar","Lamar"},
  {"Lanier","Lanier"},{"Laurens","Laurens"},{"Lee","Lee"},{"Liberty","Liberty"},{"Lincoln","Lincoln"},
  {"Long","Long"},{"Lowndes","Lowndes"},{"Lumpkin","Lumpkin"},{"Macon","Macon"},{"Madison","Madison"},
  {"Marion","Marion"},{"McDuffie","McDuffie"},{"McIntosh","McIntosh"},{"Meriwether","Meriwether"},
  {"Miller","Miller"},{"Mitchell","Mitchell"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},
  {"Morgan","Morgan"},{"Murray","Murray"},{"Muscogee","Muscogee"},{"Newton","Newton"},{"Oconee","Oconee"},
  {"Oglethorpe","Oglethorpe"},{"Paulding","Paulding"},{"Peach","Peach"},{"Pickens","Pickens"},
  {"Pierce","Pierce"},{"Pike","Pike"},{"Polk","Polk"},{"Pulaski","Pulaski"},{"Putnam","Putnam"},
  {"Quitman","Quitman"},{"Rabun","Rabun"},{"Randolph","Randolph"},{"Richmond","Richmond"},
  {"Rockdale","Rockdale"},{"Schley","Schley"},{"Screven","Screven"},{"Seminole","Seminole"},
  {"Spalding","Spalding"},{"Stephens","Stephens"},{"Stewart","Stewart"},{"Sumter","Sumter"},
  {"Talbot","Talbot"},{"Taliaferro","Taliaferro"},{"Tattnall","Tattnall"},{"Taylor","Taylor"},
  {"Telfair","Telfair"},{"Terrell","Terrell"},{"Thomas","Thomas"},{"Tift","Tift"},{"Toombs","Toombs"},
  {"Towns","Towns"},{"Treutlen","Treutlen"},{"Troup","Troup"},{"Turner","Turner"},{"Twiggs","Twiggs"},
  {"Union","Union"},{"Upson","Upson"},{"Walker","Walker"},{"Walton","Walton"},{"Ware","Ware"},
  {"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},{"Webster","Webster"},
  {"Wheeler","Wheeler"},{"White","White"},{"Whitfield","Whitfield"},{"Wilcox","Wilcox"},{"Wilkes","Wilkes"},
  {"Wilkinson","Wilkinson"},{"Worth","Worth"},
};
static const int CNTY_GA_N = 159;

static const SubdivEntry CNTY_HI[] = {
  {"Hawaii","Hawaii"},{"Honolulu","Honolulu"},{"Kalawao","Kalawao"},{"Kauai","Kauai"},{"Maui","Maui"},
};
static const int CNTY_HI_N = 5;

static const SubdivEntry CNTY_IA[] = {
  {"Adair","Adair"},{"Adams","Adams"},{"Allamakee","Allamakee"},{"Appanoose","Appanoose"},
  {"Audubon","Audubon"},{"Benton","Benton"},{"Black Hawk","Black Hawk"},{"Boone","Boone"},
  {"Bremer","Bremer"},{"Buchanan","Buchanan"},{"Buena Vista","Buena Vista"},{"Butler","Butler"},
  {"Calhoun","Calhoun"},{"Carroll","Carroll"},{"Cass","Cass"},{"Cedar","Cedar"},
  {"Cerro Gordo","Cerro Gordo"},{"Cherokee","Cherokee"},{"Chickasaw","Chickasaw"},{"Clarke","Clarke"},
  {"Clay","Clay"},{"Clayton","Clayton"},{"Clinton","Clinton"},{"Crawford","Crawford"},{"Dallas","Dallas"},
  {"Davis","Davis"},{"Decatur","Decatur"},{"Delaware","Delaware"},{"Des Moines","Des Moines"},
  {"Dickinson","Dickinson"},{"Dubuque","Dubuque"},{"Emmet","Emmet"},{"Fayette","Fayette"},{"Floyd","Floyd"},
  {"Franklin","Franklin"},{"Fremont","Fremont"},{"Greene","Greene"},{"Grundy","Grundy"},
  {"Guthrie","Guthrie"},{"Hamilton","Hamilton"},{"Hancock","Hancock"},{"Hardin","Hardin"},
  {"Harrison","Harrison"},{"Henry","Henry"},{"Howard","Howard"},{"Humboldt","Humboldt"},{"Ida","Ida"},
  {"Iowa","Iowa"},{"Jackson","Jackson"},{"Jasper","Jasper"},{"Jefferson","Jefferson"},{"Johnson","Johnson"},
  {"Jones","Jones"},{"Keokuk","Keokuk"},{"Kossuth","Kossuth"},{"Lee","Lee"},{"Linn","Linn"},
  {"Louisa","Louisa"},{"Lucas","Lucas"},{"Lyon","Lyon"},{"Madison","Madison"},{"Mahaska","Mahaska"},
  {"Marion","Marion"},{"Marshall","Marshall"},{"Mills","Mills"},{"Mitchell","Mitchell"},{"Monona","Monona"},
  {"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Muscatine","Muscatine"},{"Obrien","O&apos;Brien"},
  {"Osceola","Osceola"},{"Page","Page"},{"Palo Alto","Palo Alto"},{"Plymouth","Plymouth"},
  {"Pocahontas","Pocahontas"},{"Polk","Polk"},{"Pottawattamie","Pottawattamie"},{"Poweshiek","Poweshiek"},
  {"Ringgold","Ringgold"},{"Sac","Sac"},{"Scott","Scott"},{"Shelby","Shelby"},{"Sioux","Sioux"},
  {"Story","Story"},{"Tama","Tama"},{"Taylor","Taylor"},{"Union","Union"},{"Van Buren","Van Buren"},
  {"Wapello","Wapello"},{"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},
  {"Webster","Webster"},{"Winnebago","Winnebago"},{"Winneshiek","Winneshiek"},{"Woodbury","Woodbury"},
  {"Worth","Worth"},{"Wright","Wright"},
};
static const int CNTY_IA_N = 99;

static const SubdivEntry CNTY_ID[] = {
  {"Ada","Ada"},{"Adams","Adams"},{"Bannock","Bannock"},{"Bear Lake","Bear Lake"},{"Benewah","Benewah"},
  {"Bingham","Bingham"},{"Blaine","Blaine"},{"Boise","Boise"},{"Bonner","Bonner"},
  {"Bonneville","Bonneville"},{"Boundary","Boundary"},{"Butte","Butte"},{"Camas","Camas"},
  {"Canyon","Canyon"},{"Caribou","Caribou"},{"Cassia","Cassia"},{"Clark","Clark"},
  {"Clearwater","Clearwater"},{"Custer","Custer"},{"Elmore","Elmore"},{"Franklin","Franklin"},
  {"Fremont","Fremont"},{"Gem","Gem"},{"Gooding","Gooding"},{"Idaho","Idaho"},{"Jefferson","Jefferson"},
  {"Jerome","Jerome"},{"Kootenai","Kootenai"},{"Latah","Latah"},{"Lemhi","Lemhi"},{"Lewis","Lewis"},
  {"Lincoln","Lincoln"},{"Madison","Madison"},{"Minidoka","Minidoka"},{"Nez Perce","Nez Perce"},
  {"Oneida","Oneida"},{"Owyhee","Owyhee"},{"Payette","Payette"},{"Power","Power"},{"Shoshone","Shoshone"},
  {"Teton","Teton"},{"Twin Falls","Twin Falls"},{"Valley","Valley"},{"Washington","Washington"},
};
static const int CNTY_ID_N = 44;

static const SubdivEntry CNTY_IL[] = {
  {"Adams","Adams"},{"Alexander","Alexander"},{"Bond","Bond"},{"Boone","Boone"},{"Brown","Brown"},
  {"Bureau","Bureau"},{"Calhoun","Calhoun"},{"Carroll","Carroll"},{"Cass","Cass"},{"Champaign","Champaign"},
  {"Christian","Christian"},{"Clark","Clark"},{"Clay","Clay"},{"Clinton","Clinton"},{"Coles","Coles"},
  {"Cook","Cook"},{"Crawford","Crawford"},{"Cumberland","Cumberland"},{"De Kalb","DeKalb"},
  {"Dewitt","DeWitt"},{"Douglas","Douglas"},{"Du Page","DuPage"},{"Edgar","Edgar"},{"Edwards","Edwards"},
  {"Effingham","Effingham"},{"Fayette","Fayette"},{"Ford","Ford"},{"Franklin","Franklin"},
  {"Fulton","Fulton"},{"Gallatin","Gallatin"},{"Greene","Greene"},{"Grundy","Grundy"},
  {"Hamilton","Hamilton"},{"Hancock","Hancock"},{"Hardin","Hardin"},{"Henderson","Henderson"},
  {"Henry","Henry"},{"Iroquois","Iroquois"},{"Jackson","Jackson"},{"Jasper","Jasper"},
  {"Jefferson","Jefferson"},{"Jersey","Jersey"},{"Jo Daviess","Jo Daviess"},{"Johnson","Johnson"},
  {"Kane","Kane"},{"Kankakee","Kankakee"},{"Kendall","Kendall"},{"Knox","Knox"},{"La Salle","LaSalle"},
  {"Lake","Lake"},{"Lawrence","Lawrence"},{"Lee","Lee"},{"Livingston","Livingston"},{"Logan","Logan"},
  {"Macon","Macon"},{"Macoupin","Macoupin"},{"Madison","Madison"},{"Marion","Marion"},
  {"Marshall","Marshall"},{"Mason","Mason"},{"Massac","Massac"},{"McDonough","McDonough"},
  {"McHenry","McHenry"},{"McLean","McLean"},{"Menard","Menard"},{"Mercer","Mercer"},{"Monroe","Monroe"},
  {"Montgomery","Montgomery"},{"Morgan","Morgan"},{"Moultrie","Moultrie"},{"Ogle","Ogle"},
  {"Peoria","Peoria"},{"Perry","Perry"},{"Piatt","Piatt"},{"Pike","Pike"},{"Pope","Pope"},
  {"Pulaski","Pulaski"},{"Putnam","Putnam"},{"Randolph","Randolph"},{"Richland","Richland"},
  {"Rock Island","Rock Island"},{"Saint Clair","St. Clair"},{"Saline","Saline"},{"Sangamon","Sangamon"},
  {"Schuyler","Schuyler"},{"Scott","Scott"},{"Shelby","Shelby"},{"Stark","Stark"},
  {"Stephenson","Stephenson"},{"Tazewell","Tazewell"},{"Union","Union"},{"Vermilion","Vermilion"},
  {"Wabash","Wabash"},{"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},{"White","White"},
  {"Whiteside","Whiteside"},{"Will","Will"},{"Williamson","Williamson"},{"Winnebago","Winnebago"},
  {"Woodford","Woodford"},
};
static const int CNTY_IL_N = 102;

static const SubdivEntry CNTY_IN[] = {
  {"Adams","Adams"},{"Allen","Allen"},{"Bartholomew","Bartholomew"},{"Benton","Benton"},
  {"Blackford","Blackford"},{"Boone","Boone"},{"Brown","Brown"},{"Carroll","Carroll"},{"Cass","Cass"},
  {"Clark","Clark"},{"Clay","Clay"},{"Clinton","Clinton"},{"Crawford","Crawford"},{"Daviess","Daviess"},
  {"De Kalb","DeKalb"},{"Dearborn","Dearborn"},{"Decatur","Decatur"},{"Delaware","Delaware"},
  {"Dubois","Dubois"},{"Elkhart","Elkhart"},{"Fayette","Fayette"},{"Floyd","Floyd"},{"Fountain","Fountain"},
  {"Franklin","Franklin"},{"Fulton","Fulton"},{"Gibson","Gibson"},{"Grant","Grant"},{"Greene","Greene"},
  {"Hamilton","Hamilton"},{"Hancock","Hancock"},{"Harrison","Harrison"},{"Hendricks","Hendricks"},
  {"Henry","Henry"},{"Howard","Howard"},{"Huntington","Huntington"},{"Jackson","Jackson"},
  {"Jasper","Jasper"},{"Jay","Jay"},{"Jefferson","Jefferson"},{"Jennings","Jennings"},{"Johnson","Johnson"},
  {"Knox","Knox"},{"Kosciusko","Kosciusko"},{"La Porte","LaPorte"},{"Lagrange","LaGrange"},{"Lake","Lake"},
  {"Lawrence","Lawrence"},{"Madison","Madison"},{"Marion","Marion"},{"Marshall","Marshall"},
  {"Martin","Martin"},{"Miami","Miami"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Morgan","Morgan"},
  {"Newton","Newton"},{"Noble","Noble"},{"Ohio","Ohio"},{"Orange","Orange"},{"Owen","Owen"},
  {"Parke","Parke"},{"Perry","Perry"},{"Pike","Pike"},{"Porter","Porter"},{"Posey","Posey"},
  {"Pulaski","Pulaski"},{"Putnam","Putnam"},{"Randolph","Randolph"},{"Ripley","Ripley"},{"Rush","Rush"},
  {"Scott","Scott"},{"Shelby","Shelby"},{"Spencer","Spencer"},{"St Joseph","St. Joseph"},
  {"Starke","Starke"},{"Steuben","Steuben"},{"Sullivan","Sullivan"},{"Switzerland","Switzerland"},
  {"Tippecanoe","Tippecanoe"},{"Tipton","Tipton"},{"Union","Union"},{"Vanderburgh","Vanderburgh"},
  {"Vermillion","Vermillion"},{"Vigo","Vigo"},{"Wabash","Wabash"},{"Warren","Warren"},{"Warrick","Warrick"},
  {"Washington","Washington"},{"Wayne","Wayne"},{"Wells","Wells"},{"White","White"},{"Whitley","Whitley"},
};
static const int CNTY_IN_N = 92;

static const SubdivEntry CNTY_KS[] = {
  {"Allen","Allen"},{"Anderson","Anderson"},{"Atchison","Atchison"},{"Barber","Barber"},{"Barton","Barton"},
  {"Bourbon","Bourbon"},{"Brown","Brown"},{"Butler","Butler"},{"Chase","Chase"},{"Chautauqua","Chautauqua"},
  {"Cherokee","Cherokee"},{"Cheyenne","Cheyenne"},{"Clark","Clark"},{"Clay","Clay"},{"Cloud","Cloud"},
  {"Coffey","Coffey"},{"Comanche","Comanche"},{"Cowley","Cowley"},{"Crawford","Crawford"},
  {"Decatur","Decatur"},{"Dickinson","Dickinson"},{"Doniphan","Doniphan"},{"Douglas","Douglas"},
  {"Edwards","Edwards"},{"Elk","Elk"},{"Ellis","Ellis"},{"Ellsworth","Ellsworth"},{"Finney","Finney"},
  {"Ford","Ford"},{"Franklin","Franklin"},{"Geary","Geary"},{"Gove","Gove"},{"Graham","Graham"},
  {"Grant","Grant"},{"Gray","Gray"},{"Greeley","Greeley"},{"Greenwood","Greenwood"},{"Hamilton","Hamilton"},
  {"Harper","Harper"},{"Harvey","Harvey"},{"Haskell","Haskell"},{"Hodgeman","Hodgeman"},
  {"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Jewell","Jewell"},{"Johnson","Johnson"},
  {"Kearny","Kearny"},{"Kingman","Kingman"},{"Kiowa","Kiowa"},{"Labette","Labette"},{"Lane","Lane"},
  {"Leavenworth","Leavenworth"},{"Lincoln","Lincoln"},{"Linn","Linn"},{"Logan","Logan"},{"Lyon","Lyon"},
  {"Marion","Marion"},{"Marshall","Marshall"},{"McPherson","McPherson"},{"Meade","Meade"},{"Miami","Miami"},
  {"Mitchell","Mitchell"},{"Montgomery","Montgomery"},{"Morris","Morris"},{"Morton","Morton"},
  {"Nemaha","Nemaha"},{"Neosho","Neosho"},{"Ness","Ness"},{"Norton","Norton"},{"Osage","Osage"},
  {"Osborne","Osborne"},{"Ottawa","Ottawa"},{"Pawnee","Pawnee"},{"Phillips","Phillips"},
  {"Pottawatomie","Pottawatomie"},{"Pratt","Pratt"},{"Rawlins","Rawlins"},{"Reno","Reno"},
  {"Republic","Republic"},{"Rice","Rice"},{"Riley","Riley"},{"Rooks","Rooks"},{"Rush","Rush"},
  {"Russell","Russell"},{"Saline","Saline"},{"Scott","Scott"},{"Sedgwick","Sedgwick"},{"Seward","Seward"},
  {"Shawnee","Shawnee"},{"Sheridan","Sheridan"},{"Sherman","Sherman"},{"Smith","Smith"},
  {"Stafford","Stafford"},{"Stanton","Stanton"},{"Stevens","Stevens"},{"Sumner","Sumner"},
  {"Thomas","Thomas"},{"Trego","Trego"},{"Wabaunsee","Wabaunsee"},{"Wallace","Wallace"},
  {"Washington","Washington"},{"Wichita","Wichita"},{"Wilson","Wilson"},{"Woodson","Woodson"},
  {"Wyandotte","Wyandotte"},
};
static const int CNTY_KS_N = 105;

static const SubdivEntry CNTY_KY[] = {
  {"Adair","Adair"},{"Allen","Allen"},{"Anderson","Anderson"},{"Ballard","Ballard"},{"Barren","Barren"},
  {"Bath","Bath"},{"Bell","Bell"},{"Boone","Boone"},{"Bourbon","Bourbon"},{"Boyd","Boyd"},{"Boyle","Boyle"},
  {"Bracken","Bracken"},{"Breathitt","Breathitt"},{"Breckinridge","Breckinridge"},{"Bullitt","Bullitt"},
  {"Butler","Butler"},{"Caldwell","Caldwell"},{"Calloway","Calloway"},{"Campbell","Campbell"},
  {"Carlisle","Carlisle"},{"Carroll","Carroll"},{"Carter","Carter"},{"Casey","Casey"},
  {"Christian","Christian"},{"Clark","Clark"},{"Clay","Clay"},{"Clinton","Clinton"},
  {"Crittenden","Crittenden"},{"Cumberland","Cumberland"},{"Daviess","Daviess"},{"Edmonson","Edmonson"},
  {"Elliott","Elliott"},{"Estill","Estill"},{"Fayette","Fayette"},{"Fleming","Fleming"},{"Floyd","Floyd"},
  {"Franklin","Franklin"},{"Fulton","Fulton"},{"Gallatin","Gallatin"},{"Garrard","Garrard"},
  {"Grant","Grant"},{"Graves","Graves"},{"Grayson","Grayson"},{"Green","Green"},{"Greenup","Greenup"},
  {"Hancock","Hancock"},{"Hardin","Hardin"},{"Harlan","Harlan"},{"Harrison","Harrison"},{"Hart","Hart"},
  {"Henderson","Henderson"},{"Henry","Henry"},{"Hickman","Hickman"},{"Hopkins","Hopkins"},
  {"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Jessamine","Jessamine"},{"Johnson","Johnson"},
  {"Kenton","Kenton"},{"Knott","Knott"},{"Knox","Knox"},{"Larue","Larue"},{"Laurel","Laurel"},
  {"Lawrence","Lawrence"},{"Lee","Lee"},{"Leslie","Leslie"},{"Letcher","Letcher"},{"Lewis","Lewis"},
  {"Lincoln","Lincoln"},{"Livingston","Livingston"},{"Logan","Logan"},{"Lyon","Lyon"},{"Madison","Madison"},
  {"Magoffin","Magoffin"},{"Marion","Marion"},{"Marshall","Marshall"},{"Martin","Martin"},{"Mason","Mason"},
  {"McCracken","McCracken"},{"McCreary","McCreary"},{"McLean","McLean"},{"Meade","Meade"},
  {"Menifee","Menifee"},{"Mercer","Mercer"},{"Metcalfe","Metcalfe"},{"Monroe","Monroe"},
  {"Montgomery","Montgomery"},{"Morgan","Morgan"},{"Muhlenberg","Muhlenberg"},{"Nelson","Nelson"},
  {"Nicholas","Nicholas"},{"Ohio","Ohio"},{"Oldham","Oldham"},{"Owen","Owen"},{"Owsley","Owsley"},
  {"Pendleton","Pendleton"},{"Perry","Perry"},{"Pike","Pike"},{"Powell","Powell"},{"Pulaski","Pulaski"},
  {"Robertson","Robertson"},{"Rockcastle","Rockcastle"},{"Rowan","Rowan"},{"Russell","Russell"},
  {"Scott","Scott"},{"Shelby","Shelby"},{"Simpson","Simpson"},{"Spencer","Spencer"},{"Taylor","Taylor"},
  {"Todd","Todd"},{"Trigg","Trigg"},{"Trimble","Trimble"},{"Union","Union"},{"Warren","Warren"},
  {"Washington","Washington"},{"Wayne","Wayne"},{"Webster","Webster"},{"Whitley","Whitley"},
  {"Wolfe","Wolfe"},{"Woodford","Woodford"},
};
static const int CNTY_KY_N = 120;

static const SubdivEntry CNTY_LA[] = {
  {"Acadia","Acadia"},{"Allen","Allen"},{"Ascension","Ascension"},{"Assumption","Assumption"},
  {"Avoyelles","Avoyelles"},{"Beauregard","Beauregard"},{"Bienville","Bienville"},{"Bossier","Bossier"},
  {"Caddo","Caddo"},{"Calcasieu","Calcasieu"},{"Caldwell","Caldwell"},{"Cameron","Cameron"},
  {"Catahoula","Catahoula"},{"Claiborne","Claiborne"},{"Concordia","Concordia"},{"De Soto","De Soto"},
  {"East Baton Rouge","East Baton Rouge"},{"East Carroll","East Carroll"},
  {"East Feliciana","East Feliciana"},{"Evangeline","Evangeline"},{"Franklin","Franklin"},{"Grant","Grant"},
  {"Iberia","Iberia"},{"Iberville","Iberville"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},
  {"Jefferson Davis","Jefferson Davis"},{"La Salle","LaSalle"},{"Lafayette","Lafayette"},
  {"Lafourche","Lafourche"},{"Lincoln","Lincoln"},{"Livingston","Livingston"},{"Madison","Madison"},
  {"Morehouse","Morehouse"},{"Natchitoches","Natchitoches"},{"Orleans","Orleans"},{"Ouachita","Ouachita"},
  {"Plaquemines","Plaquemines"},{"Pointe Coupee","Pointe Coupee"},{"Rapides","Rapides"},
  {"Red River","Red River"},{"Richland","Richland"},{"Sabine","Sabine"},{"Saint Bernard","St. Bernard"},
  {"Saint Charles","St. Charles"},{"Saint Helena","St. Helena"},{"Saint James","St. James"},
  {"Saint Landry","St. Landry"},{"Saint Martin","St. Martin"},{"Saint Mary","St. Mary"},
  {"Saint Tammany","St. Tammany"},{"St John the Baptist","St. John the Baptist"},
  {"Tangipahoa","Tangipahoa"},{"Tensas","Tensas"},{"Terrebonne","Terrebonne"},{"Union","Union"},
  {"Vermilion","Vermilion"},{"Vernon","Vernon"},{"Washington","Washington"},{"Webster","Webster"},
  {"West Baton Rouge","West Baton Rouge"},{"West Carroll","West Carroll"},
  {"West Feliciana","West Feliciana"},{"Winn","Winn"},
};
static const int CNTY_LA_N = 64;

static const SubdivEntry CNTY_MA[] = {
  {"Barnstable","Barnstable"},{"Berkshire","Berkshire"},{"Bristol","Bristol"},{"Dukes","Dukes"},
  {"Essex","Essex"},{"Franklin","Franklin"},{"Hampden","Hampden"},{"Hampshire","Hampshire"},
  {"Middlesex","Middlesex"},{"Nantucket","Nantucket"},{"Norfolk","Norfolk"},{"Plymouth","Plymouth"},
  {"Suffolk","Suffolk"},{"Worcester","Worcester"},
};
static const int CNTY_MA_N = 14;

static const SubdivEntry CNTY_MD[] = {
  {"Allegany","Allegany"},{"Anne Arundel","Anne Arundel"},{"Baltimore","Baltimore"},
  {"Baltimore City","Baltimore City"},{"Calvert","Calvert"},{"Caroline","Caroline"},{"Carroll","Carroll"},
  {"Cecil","Cecil"},{"Charles","Charles"},{"Dorchester","Dorchester"},{"Frederick","Frederick"},
  {"Garrett","Garrett"},{"Harford","Harford"},{"Howard","Howard"},{"Kent","Kent"},
  {"Montgomery","Montgomery"},{"Prince Georges","Prince George&apos;s"},{"Queen Annes","Queen Anne&apos;s"},
  {"Saint Marys","St. Mary&apos;s"},{"Somerset","Somerset"},{"Talbot","Talbot"},{"Washington","Washington"},
  {"Wicomico","Wicomico"},{"Worcester","Worcester"},
};
static const int CNTY_MD_N = 24;

static const SubdivEntry CNTY_ME[] = {
  {"Androscoggin","Androscoggin"},{"Aroostook","Aroostook"},{"Cumberland","Cumberland"},
  {"Franklin","Franklin"},{"Hancock","Hancock"},{"Kennebec","Kennebec"},{"Knox","Knox"},
  {"Lincoln","Lincoln"},{"Oxford","Oxford"},{"Penobscot","Penobscot"},{"Piscataquis","Piscataquis"},
  {"Sagadahoc","Sagadahoc"},{"Somerset","Somerset"},{"Waldo","Waldo"},{"Washington","Washington"},
  {"York","York"},
};
static const int CNTY_ME_N = 16;

static const SubdivEntry CNTY_MI[] = {
  {"Alcona","Alcona"},{"Alger","Alger"},{"Allegan","Allegan"},{"Alpena","Alpena"},{"Antrim","Antrim"},
  {"Arenac","Arenac"},{"Baraga","Baraga"},{"Barry","Barry"},{"Bay","Bay"},{"Benzie","Benzie"},
  {"Berrien","Berrien"},{"Branch","Branch"},{"Calhoun","Calhoun"},{"Cass","Cass"},
  {"Charlevoix","Charlevoix"},{"Cheboygan","Cheboygan"},{"Chippewa","Chippewa"},{"Clare","Clare"},
  {"Clinton","Clinton"},{"Crawford","Crawford"},{"Delta","Delta"},{"Dickinson","Dickinson"},
  {"Eaton","Eaton"},{"Emmet","Emmet"},{"Genesee","Genesee"},{"Gladwin","Gladwin"},{"Gogebic","Gogebic"},
  {"Grand Traverse","Grand Traverse"},{"Gratiot","Gratiot"},{"Hillsdale","Hillsdale"},
  {"Houghton","Houghton"},{"Huron","Huron"},{"Ingham","Ingham"},{"Ionia","Ionia"},{"Iosco","Iosco"},
  {"Iron","Iron"},{"Isabella","Isabella"},{"Jackson","Jackson"},{"Kalamazoo","Kalamazoo"},
  {"Kalkaska","Kalkaska"},{"Kent","Kent"},{"Keweenaw","Keweenaw"},{"Lake","Lake"},{"Lapeer","Lapeer"},
  {"Leelanau","Leelanau"},{"Lenawee","Lenawee"},{"Livingston","Livingston"},{"Luce","Luce"},
  {"Mackinac","Mackinac"},{"Macomb","Macomb"},{"Manistee","Manistee"},{"Marquette","Marquette"},
  {"Mason","Mason"},{"Mecosta","Mecosta"},{"Menominee","Menominee"},{"Midland","Midland"},
  {"Missaukee","Missaukee"},{"Monroe","Monroe"},{"Montcalm","Montcalm"},{"Montmorency","Montmorency"},
  {"Muskegon","Muskegon"},{"Newaygo","Newaygo"},{"Oakland","Oakland"},{"Oceana","Oceana"},
  {"Ogemaw","Ogemaw"},{"Ontonagon","Ontonagon"},{"Osceola","Osceola"},{"Oscoda","Oscoda"},
  {"Otsego","Otsego"},{"Ottawa","Ottawa"},{"Presque Isle","Presque Isle"},{"Roscommon","Roscommon"},
  {"Saginaw","Saginaw"},{"Saint Clair","St. Clair"},{"Saint Joseph","St. Joseph"},{"Sanilac","Sanilac"},
  {"Schoolcraft","Schoolcraft"},{"Shiawassee","Shiawassee"},{"Tuscola","Tuscola"},{"Van Buren","Van Buren"},
  {"Washtenaw","Washtenaw"},{"Wayne","Wayne"},{"Wexford","Wexford"},
};
static const int CNTY_MI_N = 83;

static const SubdivEntry CNTY_MN[] = {
  {"Aitkin","Aitkin"},{"Anoka","Anoka"},{"Becker","Becker"},{"Beltrami","Beltrami"},{"Benton","Benton"},
  {"Big Stone","Big Stone"},{"Blue Earth","Blue Earth"},{"Brown","Brown"},{"Carlton","Carlton"},
  {"Carver","Carver"},{"Cass","Cass"},{"Chippewa","Chippewa"},{"Chisago","Chisago"},{"Clay","Clay"},
  {"Clearwater","Clearwater"},{"Cook","Cook"},{"Cottonwood","Cottonwood"},{"Crow Wing","Crow Wing"},
  {"Dakota","Dakota"},{"Dodge","Dodge"},{"Douglas","Douglas"},{"Faribault","Faribault"},
  {"Fillmore","Fillmore"},{"Freeborn","Freeborn"},{"Goodhue","Goodhue"},{"Grant","Grant"},
  {"Hennepin","Hennepin"},{"Houston","Houston"},{"Hubbard","Hubbard"},{"Isanti","Isanti"},
  {"Itasca","Itasca"},{"Jackson","Jackson"},{"Kanabec","Kanabec"},{"Kandiyohi","Kandiyohi"},
  {"Kittson","Kittson"},{"Koochiching","Koochiching"},{"Lac Qui Parle","Lac qui Parle"},{"Lake","Lake"},
  {"Lake of the Woods","Lake of the Woods"},{"Le Sueur","Le Sueur"},{"Lincoln","Lincoln"},{"Lyon","Lyon"},
  {"Mahnomen","Mahnomen"},{"Marshall","Marshall"},{"Martin","Martin"},{"McLeod","McLeod"},
  {"Meeker","Meeker"},{"Mille Lacs","Mille Lacs"},{"Morrison","Morrison"},{"Mower","Mower"},
  {"Murray","Murray"},{"Nicollet","Nicollet"},{"Nobles","Nobles"},{"Norman","Norman"},{"Olmsted","Olmsted"},
  {"Otter Tail","Otter Tail"},{"Pennington","Pennington"},{"Pine","Pine"},{"Pipestone","Pipestone"},
  {"Polk","Polk"},{"Pope","Pope"},{"Ramsey","Ramsey"},{"Red Lake","Red Lake"},{"Redwood","Redwood"},
  {"Renville","Renville"},{"Rice","Rice"},{"Rock","Rock"},{"Roseau","Roseau"},{"Saint Louis","St. Louis"},
  {"Scott","Scott"},{"Sherburne","Sherburne"},{"Sibley","Sibley"},{"Stearns","Stearns"},{"Steele","Steele"},
  {"Stevens","Stevens"},{"Swift","Swift"},{"Todd","Todd"},{"Traverse","Traverse"},{"Wabasha","Wabasha"},
  {"Wadena","Wadena"},{"Waseca","Waseca"},{"Washington","Washington"},{"Watonwan","Watonwan"},
  {"Wilkin","Wilkin"},{"Winona","Winona"},{"Wright","Wright"},{"Yellow Medicine","Yellow Medicine"},
};
static const int CNTY_MN_N = 87;

static const SubdivEntry CNTY_MO[] = {
  {"Adair","Adair"},{"Andrew","Andrew"},{"Atchison","Atchison"},{"Audrain","Audrain"},{"Barry","Barry"},
  {"Barton","Barton"},{"Bates","Bates"},{"Benton","Benton"},{"Bollinger","Bollinger"},{"Boone","Boone"},
  {"Buchanan","Buchanan"},{"Butler","Butler"},{"Caldwell","Caldwell"},{"Callaway","Callaway"},
  {"Camden","Camden"},{"Cape Girardeau","Cape Girardeau"},{"Carroll","Carroll"},{"Carter","Carter"},
  {"Cass","Cass"},{"Cedar","Cedar"},{"Chariton","Chariton"},{"Christian","Christian"},{"Clark","Clark"},
  {"Clay","Clay"},{"Clinton","Clinton"},{"Cole","Cole"},{"Cooper","Cooper"},{"Crawford","Crawford"},
  {"Dade","Dade"},{"Dallas","Dallas"},{"Daviess","Daviess"},{"Dekalb","DeKalb"},{"Dent","Dent"},
  {"Douglas","Douglas"},{"Dunklin","Dunklin"},{"Franklin","Franklin"},{"Gasconade","Gasconade"},
  {"Gentry","Gentry"},{"Greene","Greene"},{"Grundy","Grundy"},{"Harrison","Harrison"},{"Henry","Henry"},
  {"Hickory","Hickory"},{"Holt","Holt"},{"Howard","Howard"},{"Howell","Howell"},{"Iron","Iron"},
  {"Jackson","Jackson"},{"Jasper","Jasper"},{"Jefferson","Jefferson"},{"Johnson","Johnson"},{"Knox","Knox"},
  {"Laclede","Laclede"},{"Lafayette","Lafayette"},{"Lawrence","Lawrence"},{"Lewis","Lewis"},
  {"Lincoln","Lincoln"},{"Linn","Linn"},{"Livingston","Livingston"},{"Macon","Macon"},{"Madison","Madison"},
  {"Maries","Maries"},{"Marion","Marion"},{"McDonald","McDonald"},{"Mercer","Mercer"},{"Miller","Miller"},
  {"Mississippi","Mississippi"},{"Moniteau","Moniteau"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},
  {"Morgan","Morgan"},{"New Madrid","New Madrid"},{"Newton","Newton"},{"Nodaway","Nodaway"},
  {"Oregon","Oregon"},{"Osage","Osage"},{"Ozark","Ozark"},{"Pemiscot","Pemiscot"},{"Perry","Perry"},
  {"Pettis","Pettis"},{"Phelps","Phelps"},{"Pike","Pike"},{"Platte","Platte"},{"Polk","Polk"},
  {"Pulaski","Pulaski"},{"Putnam","Putnam"},{"Ralls","Ralls"},{"Randolph","Randolph"},{"Ray","Ray"},
  {"Reynolds","Reynolds"},{"Ripley","Ripley"},{"Saint Charles","St. Charles"},{"Saint Clair","St. Clair"},
  {"Saint Francois","St. Francois"},{"Saint Louis","St. Louis"},{"Saint Louis City","St. Louis City"},
  {"Sainte Genevieve","Ste. Genevieve"},{"Saline","Saline"},{"Schuyler","Schuyler"},{"Scotland","Scotland"},
  {"Scott","Scott"},{"Shannon","Shannon"},{"Shelby","Shelby"},{"Stoddard","Stoddard"},{"Stone","Stone"},
  {"Sullivan","Sullivan"},{"Taney","Taney"},{"Texas","Texas"},{"Vernon","Vernon"},{"Warren","Warren"},
  {"Washington","Washington"},{"Wayne","Wayne"},{"Webster","Webster"},{"Worth","Worth"},{"Wright","Wright"},
};
static const int CNTY_MO_N = 115;

static const SubdivEntry CNTY_MS[] = {
  {"Adams","Adams"},{"Alcorn","Alcorn"},{"Amite","Amite"},{"Attala","Attala"},{"Benton","Benton"},
  {"Bolivar","Bolivar"},{"Calhoun","Calhoun"},{"Carroll","Carroll"},{"Chickasaw","Chickasaw"},
  {"Choctaw","Choctaw"},{"Claiborne","Claiborne"},{"Clarke","Clarke"},{"Clay","Clay"},{"Coahoma","Coahoma"},
  {"Copiah","Copiah"},{"Covington","Covington"},{"De Soto","DeSoto"},{"Forrest","Forrest"},
  {"Franklin","Franklin"},{"George","George"},{"Greene","Greene"},{"Grenada","Grenada"},
  {"Hancock","Hancock"},{"Harrison","Harrison"},{"Hinds","Hinds"},{"Holmes","Holmes"},
  {"Humphreys","Humphreys"},{"Issaquena","Issaquena"},{"Itawamba","Itawamba"},{"Jackson","Jackson"},
  {"Jasper","Jasper"},{"Jefferson","Jefferson"},{"Jefferson Davis","Jefferson Davis"},{"Jones","Jones"},
  {"Kemper","Kemper"},{"Lafayette","Lafayette"},{"Lamar","Lamar"},{"Lauderdale","Lauderdale"},
  {"Lawrence","Lawrence"},{"Leake","Leake"},{"Lee","Lee"},{"Leflore","Leflore"},{"Lincoln","Lincoln"},
  {"Lowndes","Lowndes"},{"Madison","Madison"},{"Marion","Marion"},{"Marshall","Marshall"},
  {"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Neshoba","Neshoba"},{"Newton","Newton"},
  {"Noxubee","Noxubee"},{"Oktibbeha","Oktibbeha"},{"Panola","Panola"},{"Pearl River","Pearl River"},
  {"Perry","Perry"},{"Pike","Pike"},{"Pontotoc","Pontotoc"},{"Prentiss","Prentiss"},{"Quitman","Quitman"},
  {"Rankin","Rankin"},{"Scott","Scott"},{"Sharkey","Sharkey"},{"Simpson","Simpson"},{"Smith","Smith"},
  {"Stone","Stone"},{"Sunflower","Sunflower"},{"Tallahatchie","Tallahatchie"},{"Tate","Tate"},
  {"Tippah","Tippah"},{"Tishomingo","Tishomingo"},{"Tunica","Tunica"},{"Union","Union"},
  {"Walthall","Walthall"},{"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},
  {"Webster","Webster"},{"Wilkinson","Wilkinson"},{"Winston","Winston"},{"Yalobusha","Yalobusha"},
  {"Yazoo","Yazoo"},
};
static const int CNTY_MS_N = 82;

static const SubdivEntry CNTY_MT[] = {
  {"Beaverhead","Beaverhead"},{"Big Horn","Big Horn"},{"Blaine","Blaine"},{"Broadwater","Broadwater"},
  {"Carbon","Carbon"},{"Carter","Carter"},{"Cascade","Cascade"},{"Chouteau","Chouteau"},{"Custer","Custer"},
  {"Daniels","Daniels"},{"Dawson","Dawson"},{"Deer Lodge","Deer Lodge"},{"Fallon","Fallon"},
  {"Fergus","Fergus"},{"Flathead","Flathead"},{"Gallatin","Gallatin"},{"Garfield","Garfield"},
  {"Glacier","Glacier"},{"Golden Valley","Golden Valley"},{"Granite","Granite"},{"Hill","Hill"},
  {"Jefferson","Jefferson"},{"Judith Basin","Judith Basin"},{"Lake","Lake"},
  {"Lewis and Clark","Lewis and Clark"},{"Liberty","Liberty"},{"Lincoln","Lincoln"},{"Madison","Madison"},
  {"McCone","McCone"},{"Meagher","Meagher"},{"Mineral","Mineral"},{"Missoula","Missoula"},
  {"Musselshell","Musselshell"},{"Park","Park"},{"Petroleum","Petroleum"},{"Phillips","Phillips"},
  {"Pondera","Pondera"},{"Powder River","Powder River"},{"Powell","Powell"},{"Prairie","Prairie"},
  {"Ravalli","Ravalli"},{"Richland","Richland"},{"Roosevelt","Roosevelt"},{"Rosebud","Rosebud"},
  {"Sanders","Sanders"},{"Sheridan","Sheridan"},{"Silver Bow","Silver Bow"},{"Stillwater","Stillwater"},
  {"Sweet Grass","Sweet Grass"},{"Teton","Teton"},{"Toole","Toole"},{"Treasure","Treasure"},
  {"Valley","Valley"},{"Wheatland","Wheatland"},{"Wibaux","Wibaux"},{"Yellowstone","Yellowstone"},
};
static const int CNTY_MT_N = 56;

static const SubdivEntry CNTY_NC[] = {
  {"Alamance","Alamance"},{"Alexander","Alexander"},{"Alleghany","Alleghany"},{"Anson","Anson"},
  {"Ashe","Ashe"},{"Avery","Avery"},{"Beaufort","Beaufort"},{"Bertie","Bertie"},{"Bladen","Bladen"},
  {"Brunswick","Brunswick"},{"Buncombe","Buncombe"},{"Burke","Burke"},{"Cabarrus","Cabarrus"},
  {"Caldwell","Caldwell"},{"Camden","Camden"},{"Carteret","Carteret"},{"Caswell","Caswell"},
  {"Catawba","Catawba"},{"Chatham","Chatham"},{"Cherokee","Cherokee"},{"Chowan","Chowan"},{"Clay","Clay"},
  {"Cleveland","Cleveland"},{"Columbus","Columbus"},{"Craven","Craven"},{"Cumberland","Cumberland"},
  {"Currituck","Currituck"},{"Dare","Dare"},{"Davidson","Davidson"},{"Davie","Davie"},{"Duplin","Duplin"},
  {"Durham","Durham"},{"Edgecombe","Edgecombe"},{"Forsyth","Forsyth"},{"Franklin","Franklin"},
  {"Gaston","Gaston"},{"Gates","Gates"},{"Graham","Graham"},{"Granville","Granville"},{"Greene","Greene"},
  {"Guilford","Guilford"},{"Halifax","Halifax"},{"Harnett","Harnett"},{"Haywood","Haywood"},
  {"Henderson","Henderson"},{"Hertford","Hertford"},{"Hoke","Hoke"},{"Hyde","Hyde"},{"Iredell","Iredell"},
  {"Jackson","Jackson"},{"Johnston","Johnston"},{"Jones","Jones"},{"Lee","Lee"},{"Lenoir","Lenoir"},
  {"Lincoln","Lincoln"},{"Macon","Macon"},{"Madison","Madison"},{"Martin","Martin"},{"McDowell","McDowell"},
  {"Mecklenburg","Mecklenburg"},{"Mitchell","Mitchell"},{"Montgomery","Montgomery"},{"Moore","Moore"},
  {"Nash","Nash"},{"New Hanover","New Hanover"},{"Northampton","Northampton"},{"Onslow","Onslow"},
  {"Orange","Orange"},{"Pamlico","Pamlico"},{"Pasquotank","Pasquotank"},{"Pender","Pender"},
  {"Perquimans","Perquimans"},{"Person","Person"},{"Pitt","Pitt"},{"Polk","Polk"},{"Randolph","Randolph"},
  {"Richmond","Richmond"},{"Robeson","Robeson"},{"Rockingham","Rockingham"},{"Rowan","Rowan"},
  {"Rutherford","Rutherford"},{"Sampson","Sampson"},{"Scotland","Scotland"},{"Stanly","Stanly"},
  {"Stokes","Stokes"},{"Surry","Surry"},{"Swain","Swain"},{"Transylvania","Transylvania"},
  {"Tyrrell","Tyrrell"},{"Union","Union"},{"Vance","Vance"},{"Wake","Wake"},{"Warren","Warren"},
  {"Washington","Washington"},{"Watauga","Watauga"},{"Wayne","Wayne"},{"Wilkes","Wilkes"},
  {"Wilson","Wilson"},{"Yadkin","Yadkin"},{"Yancey","Yancey"},
};
static const int CNTY_NC_N = 100;

static const SubdivEntry CNTY_ND[] = {
  {"Adams","Adams"},{"Barnes","Barnes"},{"Benson","Benson"},{"Billings","Billings"},
  {"Bottineau","Bottineau"},{"Bowman","Bowman"},{"Burke","Burke"},{"Burleigh","Burleigh"},{"Cass","Cass"},
  {"Cavalier","Cavalier"},{"Dickey","Dickey"},{"Divide","Divide"},{"Dunn","Dunn"},{"Eddy","Eddy"},
  {"Emmons","Emmons"},{"Foster","Foster"},{"Golden Valley","Golden Valley"},{"Grand Forks","Grand Forks"},
  {"Grant","Grant"},{"Griggs","Griggs"},{"Hettinger","Hettinger"},{"Kidder","Kidder"},{"Lamoure","LaMoure"},
  {"Logan","Logan"},{"McHenry","McHenry"},{"McIntosh","McIntosh"},{"McKenzie","McKenzie"},
  {"McLean","McLean"},{"Mercer","Mercer"},{"Morton","Morton"},{"Mountrail","Mountrail"},{"Nelson","Nelson"},
  {"Oliver","Oliver"},{"Pembina","Pembina"},{"Pierce","Pierce"},{"Ramsey","Ramsey"},{"Ransom","Ransom"},
  {"Renville","Renville"},{"Richland","Richland"},{"Rolette","Rolette"},{"Sargent","Sargent"},
  {"Sheridan","Sheridan"},{"Sioux","Sioux"},{"Slope","Slope"},{"Stark","Stark"},{"Steele","Steele"},
  {"Stutsman","Stutsman"},{"Towner","Towner"},{"Traill","Traill"},{"Walsh","Walsh"},{"Ward","Ward"},
  {"Wells","Wells"},{"Williams","Williams"},
};
static const int CNTY_ND_N = 53;

static const SubdivEntry CNTY_NE[] = {
  {"Adams","Adams"},{"Antelope","Antelope"},{"Arthur","Arthur"},{"Banner","Banner"},{"Blaine","Blaine"},
  {"Boone","Boone"},{"Box Butte","Box Butte"},{"Boyd","Boyd"},{"Brown","Brown"},{"Buffalo","Buffalo"},
  {"Burt","Burt"},{"Butler","Butler"},{"Cass","Cass"},{"Cedar","Cedar"},{"Chase","Chase"},
  {"Cherry","Cherry"},{"Cheyenne","Cheyenne"},{"Clay","Clay"},{"Colfax","Colfax"},{"Cuming","Cuming"},
  {"Custer","Custer"},{"Dakota","Dakota"},{"Dawes","Dawes"},{"Dawson","Dawson"},{"Deuel","Deuel"},
  {"Dixon","Dixon"},{"Dodge","Dodge"},{"Douglas","Douglas"},{"Dundy","Dundy"},{"Fillmore","Fillmore"},
  {"Franklin","Franklin"},{"Frontier","Frontier"},{"Furnas","Furnas"},{"Gage","Gage"},{"Garden","Garden"},
  {"Garfield","Garfield"},{"Gosper","Gosper"},{"Grant","Grant"},{"Greeley","Greeley"},{"Hall","Hall"},
  {"Hamilton","Hamilton"},{"Harlan","Harlan"},{"Hayes","Hayes"},{"Hitchcock","Hitchcock"},{"Holt","Holt"},
  {"Hooker","Hooker"},{"Howard","Howard"},{"Jefferson","Jefferson"},{"Johnson","Johnson"},
  {"Kearney","Kearney"},{"Keith","Keith"},{"Keya Paha","Keya Paha"},{"Kimball","Kimball"},{"Knox","Knox"},
  {"Lancaster","Lancaster"},{"Lincoln","Lincoln"},{"Logan","Logan"},{"Loup","Loup"},{"Madison","Madison"},
  {"McPherson","McPherson"},{"Merrick","Merrick"},{"Morrill","Morrill"},{"Nance","Nance"},
  {"Nemaha","Nemaha"},{"Nuckolls","Nuckolls"},{"Otoe","Otoe"},{"Pawnee","Pawnee"},{"Perkins","Perkins"},
  {"Phelps","Phelps"},{"Pierce","Pierce"},{"Platte","Platte"},{"Polk","Polk"},{"Red Willow","Red Willow"},
  {"Richardson","Richardson"},{"Rock","Rock"},{"Saline","Saline"},{"Sarpy","Sarpy"},{"Saunders","Saunders"},
  {"Scotts Bluff","Scotts Bluff"},{"Seward","Seward"},{"Sheridan","Sheridan"},{"Sherman","Sherman"},
  {"Sioux","Sioux"},{"Stanton","Stanton"},{"Thayer","Thayer"},{"Thomas","Thomas"},{"Thurston","Thurston"},
  {"Valley","Valley"},{"Washington","Washington"},{"Wayne","Wayne"},{"Webster","Webster"},
  {"Wheeler","Wheeler"},{"York","York"},
};
static const int CNTY_NE_N = 93;

static const SubdivEntry CNTY_NH[] = {
  {"Belknap","Belknap"},{"Carroll","Carroll"},{"Cheshire","Cheshire"},{"Coos","Coos"},{"Grafton","Grafton"},
  {"Hillsborough","Hillsborough"},{"Merrimack","Merrimack"},{"Rockingham","Rockingham"},
  {"Strafford","Strafford"},{"Sullivan","Sullivan"},
};
static const int CNTY_NH_N = 10;

static const SubdivEntry CNTY_NJ[] = {
  {"Atlantic","Atlantic"},{"Bergen","Bergen"},{"Burlington","Burlington"},{"Camden","Camden"},
  {"Cape May","Cape May"},{"Cumberland","Cumberland"},{"Essex","Essex"},{"Gloucester","Gloucester"},
  {"Hudson","Hudson"},{"Hunterdon","Hunterdon"},{"Mercer","Mercer"},{"Middlesex","Middlesex"},
  {"Monmouth","Monmouth"},{"Morris","Morris"},{"Ocean","Ocean"},{"Passaic","Passaic"},{"Salem","Salem"},
  {"Somerset","Somerset"},{"Sussex","Sussex"},{"Union","Union"},{"Warren","Warren"},
};
static const int CNTY_NJ_N = 21;

static const SubdivEntry CNTY_NM[] = {
  {"Bernalillo","Bernalillo"},{"Catron","Catron"},{"Chaves","Chaves"},{"Cibola","Cibola"},
  {"Colfax","Colfax"},{"Curry","Curry"},{"De Baca","De Baca"},{"Dona Ana","Doña Ana"},{"Eddy","Eddy"},
  {"Grant","Grant"},{"Guadalupe","Guadalupe"},{"Harding","Harding"},{"Hidalgo","Hidalgo"},{"Lea","Lea"},
  {"Lincoln","Lincoln"},{"Los Alamos","Los Alamos"},{"Luna","Luna"},{"McKinley","McKinley"},{"Mora","Mora"},
  {"Otero","Otero"},{"Quay","Quay"},{"Rio Arriba","Rio Arriba"},{"Roosevelt","Roosevelt"},
  {"San Juan","San Juan"},{"San Miguel","San Miguel"},{"Sandoval","Sandoval"},{"Santa Fe","Santa Fe"},
  {"Sierra","Sierra"},{"Socorro","Socorro"},{"Taos","Taos"},{"Torrance","Torrance"},{"Union","Union"},
  {"Valencia","Valencia"},
};
static const int CNTY_NM_N = 33;

static const SubdivEntry CNTY_NV[] = {
  {"Carson City","Carson City"},{"Churchill","Churchill"},{"Clark","Clark"},{"Douglas","Douglas"},
  {"Elko","Elko"},{"Esmeralda","Esmeralda"},{"Eureka","Eureka"},{"Humboldt","Humboldt"},{"Lander","Lander"},
  {"Lincoln","Lincoln"},{"Lyon","Lyon"},{"Mineral","Mineral"},{"Nye","Nye"},{"Pershing","Pershing"},
  {"Storey","Storey"},{"Washoe","Washoe"},{"White Pine","White Pine"},
};
static const int CNTY_NV_N = 17;

static const SubdivEntry CNTY_NY[] = {
  {"Albany","Albany"},{"Allegany","Allegany"},{"Bronx","Bronx"},{"Broome","Broome"},
  {"Cattaraugus","Cattaraugus"},{"Cayuga","Cayuga"},{"Chautauqua","Chautauqua"},{"Chemung","Chemung"},
  {"Chenango","Chenango"},{"Clinton","Clinton"},{"Columbia","Columbia"},{"Cortland","Cortland"},
  {"Delaware","Delaware"},{"Dutchess","Dutchess"},{"Erie","Erie"},{"Essex","Essex"},{"Franklin","Franklin"},
  {"Fulton","Fulton"},{"Genesee","Genesee"},{"Greene","Greene"},{"Hamilton","Hamilton"},
  {"Herkimer","Herkimer"},{"Jefferson","Jefferson"},{"Kings","Kings"},{"Lewis","Lewis"},
  {"Livingston","Livingston"},{"Madison","Madison"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},
  {"Nassau","Nassau"},{"New York","New York"},{"Niagara","Niagara"},{"Oneida","Oneida"},
  {"Onondaga","Onondaga"},{"Ontario","Ontario"},{"Orange","Orange"},{"Orleans","Orleans"},
  {"Oswego","Oswego"},{"Otsego","Otsego"},{"Putnam","Putnam"},{"Queens","Queens"},
  {"Rensselaer","Rensselaer"},{"Richmond","Richmond"},{"Rockland","Rockland"},
  {"Saint Lawrence","St. Lawrence"},{"Saratoga","Saratoga"},{"Schenectady","Schenectady"},
  {"Schoharie","Schoharie"},{"Schuyler","Schuyler"},{"Seneca","Seneca"},{"Steuben","Steuben"},
  {"Suffolk","Suffolk"},{"Sullivan","Sullivan"},{"Tioga","Tioga"},{"Tompkins","Tompkins"},
  {"Ulster","Ulster"},{"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},
  {"Westchester","Westchester"},{"Wyoming","Wyoming"},{"Yates","Yates"},
};
static const int CNTY_NY_N = 62;

static const SubdivEntry CNTY_OH[] = {
  {"Adams","Adams"},{"Allen","Allen"},{"Ashland","Ashland"},{"Ashtabula","Ashtabula"},{"Athens","Athens"},
  {"Auglaize","Auglaize"},{"Belmont","Belmont"},{"Brown","Brown"},{"Butler","Butler"},{"Carroll","Carroll"},
  {"Champaign","Champaign"},{"Clark","Clark"},{"Clermont","Clermont"},{"Clinton","Clinton"},
  {"Columbiana","Columbiana"},{"Coshocton","Coshocton"},{"Crawford","Crawford"},{"Cuyahoga","Cuyahoga"},
  {"Darke","Darke"},{"Defiance","Defiance"},{"Delaware","Delaware"},{"Erie","Erie"},
  {"Fairfield","Fairfield"},{"Fayette","Fayette"},{"Franklin","Franklin"},{"Fulton","Fulton"},
  {"Gallia","Gallia"},{"Geauga","Geauga"},{"Greene","Greene"},{"Guernsey","Guernsey"},
  {"Hamilton","Hamilton"},{"Hancock","Hancock"},{"Hardin","Hardin"},{"Harrison","Harrison"},
  {"Henry","Henry"},{"Highland","Highland"},{"Hocking","Hocking"},{"Holmes","Holmes"},{"Huron","Huron"},
  {"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Knox","Knox"},{"Lake","Lake"},{"Lawrence","Lawrence"},
  {"Licking","Licking"},{"Logan","Logan"},{"Lorain","Lorain"},{"Lucas","Lucas"},{"Madison","Madison"},
  {"Mahoning","Mahoning"},{"Marion","Marion"},{"Medina","Medina"},{"Meigs","Meigs"},{"Mercer","Mercer"},
  {"Miami","Miami"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Morgan","Morgan"},{"Morrow","Morrow"},
  {"Muskingum","Muskingum"},{"Noble","Noble"},{"Ottawa","Ottawa"},{"Paulding","Paulding"},{"Perry","Perry"},
  {"Pickaway","Pickaway"},{"Pike","Pike"},{"Portage","Portage"},{"Preble","Preble"},{"Putnam","Putnam"},
  {"Richland","Richland"},{"Ross","Ross"},{"Sandusky","Sandusky"},{"Scioto","Scioto"},{"Seneca","Seneca"},
  {"Shelby","Shelby"},{"Stark","Stark"},{"Summit","Summit"},{"Trumbull","Trumbull"},
  {"Tuscarawas","Tuscarawas"},{"Union","Union"},{"Van Wert","Van Wert"},{"Vinton","Vinton"},
  {"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},{"Williams","Williams"},{"Wood","Wood"},
  {"Wyandot","Wyandot"},
};
static const int CNTY_OH_N = 88;

static const SubdivEntry CNTY_OK[] = {
  {"Adair","Adair"},{"Alfalfa","Alfalfa"},{"Atoka","Atoka"},{"Beaver","Beaver"},{"Beckham","Beckham"},
  {"Blaine","Blaine"},{"Bryan","Bryan"},{"Caddo","Caddo"},{"Canadian","Canadian"},{"Carter","Carter"},
  {"Cherokee","Cherokee"},{"Choctaw","Choctaw"},{"Cimarron","Cimarron"},{"Cleveland","Cleveland"},
  {"Coal","Coal"},{"Comanche","Comanche"},{"Cotton","Cotton"},{"Craig","Craig"},{"Creek","Creek"},
  {"Custer","Custer"},{"Delaware","Delaware"},{"Dewey","Dewey"},{"Ellis","Ellis"},{"Garfield","Garfield"},
  {"Garvin","Garvin"},{"Grady","Grady"},{"Grant","Grant"},{"Greer","Greer"},{"Harmon","Harmon"},
  {"Harper","Harper"},{"Haskell","Haskell"},{"Hughes","Hughes"},{"Jackson","Jackson"},
  {"Jefferson","Jefferson"},{"Johnston","Johnston"},{"Kay","Kay"},{"Kingfisher","Kingfisher"},
  {"Kiowa","Kiowa"},{"Latimer","Latimer"},{"Le Flore","Le Flore"},{"Lincoln","Lincoln"},{"Logan","Logan"},
  {"Love","Love"},{"Major","Major"},{"Marshall","Marshall"},{"Mayes","Mayes"},{"McClain","McClain"},
  {"McCurtain","McCurtain"},{"McIntosh","McIntosh"},{"Murray","Murray"},{"Muskogee","Muskogee"},
  {"Noble","Noble"},{"Nowata","Nowata"},{"Okfuskee","Okfuskee"},{"Oklahoma","Oklahoma"},
  {"Okmulgee","Okmulgee"},{"Osage","Osage"},{"Ottawa","Ottawa"},{"Pawnee","Pawnee"},{"Payne","Payne"},
  {"Pittsburg","Pittsburg"},{"Pontotoc","Pontotoc"},{"Pottawatomie","Pottawatomie"},
  {"Pushmataha","Pushmataha"},{"Roger Mills","Roger Mills"},{"Rogers","Rogers"},{"Seminole","Seminole"},
  {"Sequoyah","Sequoyah"},{"Stephens","Stephens"},{"Texas","Texas"},{"Tillman","Tillman"},{"Tulsa","Tulsa"},
  {"Wagoner","Wagoner"},{"Washington","Washington"},{"Washita","Washita"},{"Woods","Woods"},
  {"Woodward","Woodward"},
};
static const int CNTY_OK_N = 77;

static const SubdivEntry CNTY_OR[] = {
  {"Baker","Baker"},{"Benton","Benton"},{"Clackamas","Clackamas"},{"Clatsop","Clatsop"},
  {"Columbia","Columbia"},{"Coos","Coos"},{"Crook","Crook"},{"Curry","Curry"},{"Deschutes","Deschutes"},
  {"Douglas","Douglas"},{"Gilliam","Gilliam"},{"Grant","Grant"},{"Harney","Harney"},
  {"Hood River","Hood River"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Josephine","Josephine"},
  {"Klamath","Klamath"},{"Lake","Lake"},{"Lane","Lane"},{"Lincoln","Lincoln"},{"Linn","Linn"},
  {"Malheur","Malheur"},{"Marion","Marion"},{"Morrow","Morrow"},{"Multnomah","Multnomah"},{"Polk","Polk"},
  {"Sherman","Sherman"},{"Tillamook","Tillamook"},{"Umatilla","Umatilla"},{"Union","Union"},
  {"Wallowa","Wallowa"},{"Wasco","Wasco"},{"Washington","Washington"},{"Wheeler","Wheeler"},
  {"Yamhill","Yamhill"},
};
static const int CNTY_OR_N = 36;

static const SubdivEntry CNTY_PA[] = {
  {"Adams","Adams"},{"Allegheny","Allegheny"},{"Armstrong","Armstrong"},{"Beaver","Beaver"},
  {"Bedford","Bedford"},{"Berks","Berks"},{"Blair","Blair"},{"Bradford","Bradford"},{"Bucks","Bucks"},
  {"Butler","Butler"},{"Cambria","Cambria"},{"Cameron","Cameron"},{"Carbon","Carbon"},{"Centre","Centre"},
  {"Chester","Chester"},{"Clarion","Clarion"},{"Clearfield","Clearfield"},{"Clinton","Clinton"},
  {"Columbia","Columbia"},{"Crawford","Crawford"},{"Cumberland","Cumberland"},{"Dauphin","Dauphin"},
  {"Delaware","Delaware"},{"Elk","Elk"},{"Erie","Erie"},{"Fayette","Fayette"},{"Forest","Forest"},
  {"Franklin","Franklin"},{"Fulton","Fulton"},{"Greene","Greene"},{"Huntingdon","Huntingdon"},
  {"Indiana","Indiana"},{"Jefferson","Jefferson"},{"Juniata","Juniata"},{"Lackawanna","Lackawanna"},
  {"Lancaster","Lancaster"},{"Lawrence","Lawrence"},{"Lebanon","Lebanon"},{"Lehigh","Lehigh"},
  {"Luzerne","Luzerne"},{"Lycoming","Lycoming"},{"McKean","McKean"},{"Mercer","Mercer"},
  {"Mifflin","Mifflin"},{"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Montour","Montour"},
  {"Northampton","Northampton"},{"Northumberland","Northumberland"},{"Perry","Perry"},
  {"Philadelphia","Philadelphia"},{"Pike","Pike"},{"Potter","Potter"},{"Schuylkill","Schuylkill"},
  {"Snyder","Snyder"},{"Somerset","Somerset"},{"Sullivan","Sullivan"},{"Susquehanna","Susquehanna"},
  {"Tioga","Tioga"},{"Union","Union"},{"Venango","Venango"},{"Warren","Warren"},{"Washington","Washington"},
  {"Wayne","Wayne"},{"Westmoreland","Westmoreland"},{"Wyoming","Wyoming"},{"York","York"},
};
static const int CNTY_PA_N = 67;

static const SubdivEntry CNTY_RI[] = {
  {"Bristol","Bristol"},{"Kent","Kent"},{"Newport","Newport"},{"Providence","Providence"},
  {"Washington","Washington"},
};
static const int CNTY_RI_N = 5;

static const SubdivEntry CNTY_SC[] = {
  {"Abbeville","Abbeville"},{"Aiken","Aiken"},{"Allendale","Allendale"},{"Anderson","Anderson"},
  {"Bamberg","Bamberg"},{"Barnwell","Barnwell"},{"Beaufort","Beaufort"},{"Berkeley","Berkeley"},
  {"Calhoun","Calhoun"},{"Charleston","Charleston"},{"Cherokee","Cherokee"},{"Chester","Chester"},
  {"Chesterfield","Chesterfield"},{"Clarendon","Clarendon"},{"Colleton","Colleton"},
  {"Darlington","Darlington"},{"Dillon","Dillon"},{"Dorchester","Dorchester"},{"Edgefield","Edgefield"},
  {"Fairfield","Fairfield"},{"Florence","Florence"},{"Georgetown","Georgetown"},{"Greenville","Greenville"},
  {"Greenwood","Greenwood"},{"Hampton","Hampton"},{"Horry","Horry"},{"Jasper","Jasper"},
  {"Kershaw","Kershaw"},{"Lancaster","Lancaster"},{"Laurens","Laurens"},{"Lee","Lee"},
  {"Lexington","Lexington"},{"Marion","Marion"},{"Marlboro","Marlboro"},{"McCormick","McCormick"},
  {"Newberry","Newberry"},{"Oconee","Oconee"},{"Orangeburg","Orangeburg"},{"Pickens","Pickens"},
  {"Richland","Richland"},{"Saluda","Saluda"},{"Spartanburg","Spartanburg"},{"Sumter","Sumter"},
  {"Union","Union"},{"Williamsburg","Williamsburg"},{"York","York"},
};
static const int CNTY_SC_N = 46;

static const SubdivEntry CNTY_SD[] = {
  {"Aurora","Aurora"},{"Beadle","Beadle"},{"Bennett","Bennett"},{"Bon Homme","Bon Homme"},
  {"Brookings","Brookings"},{"Brown","Brown"},{"Brule","Brule"},{"Buffalo","Buffalo"},{"Butte","Butte"},
  {"Campbell","Campbell"},{"Charles Mix","Charles Mix"},{"Clark","Clark"},{"Clay","Clay"},
  {"Codington","Codington"},{"Corson","Corson"},{"Custer","Custer"},{"Davison","Davison"},{"Day","Day"},
  {"Deuel","Deuel"},{"Dewey","Dewey"},{"Douglas","Douglas"},{"Edmunds","Edmunds"},
  {"Fall River","Fall River"},{"Faulk","Faulk"},{"Grant","Grant"},{"Gregory","Gregory"},{"Haakon","Haakon"},
  {"Hamlin","Hamlin"},{"Hand","Hand"},{"Hanson","Hanson"},{"Harding","Harding"},{"Hughes","Hughes"},
  {"Hutchinson","Hutchinson"},{"Hyde","Hyde"},{"Jackson","Jackson"},{"Jerauld","Jerauld"},{"Jones","Jones"},
  {"Kingsbury","Kingsbury"},{"Lake","Lake"},{"Lawrence","Lawrence"},{"Lincoln","Lincoln"},{"Lyman","Lyman"},
  {"Marshall","Marshall"},{"McCook","McCook"},{"McPherson","McPherson"},{"Meade","Meade"},
  {"Mellette","Mellette"},{"Miner","Miner"},{"Minnehaha","Minnehaha"},{"Moody","Moody"},
  {"Oglala Lakota","Oglala Lakota"},{"Pennington","Pennington"},{"Perkins","Perkins"},{"Potter","Potter"},
  {"Roberts","Roberts"},{"Sanborn","Sanborn"},{"Shannon","Shannon"},{"Spink","Spink"},{"Stanley","Stanley"},
  {"Sully","Sully"},{"Todd","Todd"},{"Tripp","Tripp"},{"Turner","Turner"},{"Union","Union"},
  {"Walworth","Walworth"},{"Yankton","Yankton"},{"Ziebach","Ziebach"},
};
static const int CNTY_SD_N = 67;

static const SubdivEntry CNTY_TN[] = {
  {"Anderson","Anderson"},{"Bedford","Bedford"},{"Benton","Benton"},{"Bledsoe","Bledsoe"},
  {"Blount","Blount"},{"Bradley","Bradley"},{"Campbell","Campbell"},{"Cannon","Cannon"},
  {"Carroll","Carroll"},{"Carter","Carter"},{"Cheatham","Cheatham"},{"Chester","Chester"},
  {"Claiborne","Claiborne"},{"Clay","Clay"},{"Cocke","Cocke"},{"Coffee","Coffee"},{"Crockett","Crockett"},
  {"Cumberland","Cumberland"},{"Davidson","Davidson"},{"Decatur","Decatur"},{"Dekalb","DeKalb"},
  {"Dickson","Dickson"},{"Dyer","Dyer"},{"Fayette","Fayette"},{"Fentress","Fentress"},
  {"Franklin","Franklin"},{"Gibson","Gibson"},{"Giles","Giles"},{"Grainger","Grainger"},{"Greene","Greene"},
  {"Grundy","Grundy"},{"Hamblen","Hamblen"},{"Hamilton","Hamilton"},{"Hancock","Hancock"},
  {"Hardeman","Hardeman"},{"Hardin","Hardin"},{"Hawkins","Hawkins"},{"Haywood","Haywood"},
  {"Henderson","Henderson"},{"Henry","Henry"},{"Hickman","Hickman"},{"Houston","Houston"},
  {"Humphreys","Humphreys"},{"Jackson","Jackson"},{"Jefferson","Jefferson"},{"Johnson","Johnson"},
  {"Knox","Knox"},{"Lake","Lake"},{"Lauderdale","Lauderdale"},{"Lawrence","Lawrence"},{"Lewis","Lewis"},
  {"Lincoln","Lincoln"},{"Loudon","Loudon"},{"Macon","Macon"},{"Madison","Madison"},{"Marion","Marion"},
  {"Marshall","Marshall"},{"Maury","Maury"},{"McMinn","McMinn"},{"McNairy","McNairy"},{"Meigs","Meigs"},
  {"Monroe","Monroe"},{"Montgomery","Montgomery"},{"Moore","Moore"},{"Morgan","Morgan"},{"Obion","Obion"},
  {"Overton","Overton"},{"Perry","Perry"},{"Pickett","Pickett"},{"Polk","Polk"},{"Putnam","Putnam"},
  {"Rhea","Rhea"},{"Roane","Roane"},{"Robertson","Robertson"},{"Rutherford","Rutherford"},{"Scott","Scott"},
  {"Sequatchie","Sequatchie"},{"Sevier","Sevier"},{"Shelby","Shelby"},{"Smith","Smith"},
  {"Stewart","Stewart"},{"Sullivan","Sullivan"},{"Sumner","Sumner"},{"Tipton","Tipton"},
  {"Trousdale","Trousdale"},{"Unicoi","Unicoi"},{"Union","Union"},{"Van Buren","Van Buren"},
  {"Warren","Warren"},{"Washington","Washington"},{"Wayne","Wayne"},{"Weakley","Weakley"},{"White","White"},
  {"Williamson","Williamson"},{"Wilson","Wilson"},
};
static const int CNTY_TN_N = 95;

static const SubdivEntry CNTY_TX[] = {
  {"Anderson","Anderson"},{"Andrews","Andrews"},{"Angelina","Angelina"},{"Aransas","Aransas"},
  {"Archer","Archer"},{"Armstrong","Armstrong"},{"Atascosa","Atascosa"},{"Austin","Austin"},
  {"Bailey","Bailey"},{"Bandera","Bandera"},{"Bastrop","Bastrop"},{"Baylor","Baylor"},{"Bee","Bee"},
  {"Bell","Bell"},{"Bexar","Bexar"},{"Blanco","Blanco"},{"Borden","Borden"},{"Bosque","Bosque"},
  {"Bowie","Bowie"},{"Brazoria","Brazoria"},{"Brazos","Brazos"},{"Brewster","Brewster"},
  {"Briscoe","Briscoe"},{"Brooks","Brooks"},{"Brown","Brown"},{"Burleson","Burleson"},{"Burnet","Burnet"},
  {"Caldwell","Caldwell"},{"Calhoun","Calhoun"},{"Callahan","Callahan"},{"Cameron","Cameron"},
  {"Camp","Camp"},{"Carson","Carson"},{"Cass","Cass"},{"Castro","Castro"},{"Chambers","Chambers"},
  {"Cherokee","Cherokee"},{"Childress","Childress"},{"Clay","Clay"},{"Cochran","Cochran"},{"Coke","Coke"},
  {"Coleman","Coleman"},{"Collin","Collin"},{"Collingsworth","Collingsworth"},{"Colorado","Colorado"},
  {"Comal","Comal"},{"Comanche","Comanche"},{"Concho","Concho"},{"Cooke","Cooke"},{"Coryell","Coryell"},
  {"Cottle","Cottle"},{"Crane","Crane"},{"Crockett","Crockett"},{"Crosby","Crosby"},
  {"Culberson","Culberson"},{"Dallam","Dallam"},{"Dallas","Dallas"},{"Dawson","Dawson"},
  {"De Witt","DeWitt"},{"Deaf Smith","Deaf Smith"},{"Delta","Delta"},{"Denton","Denton"},
  {"Dickens","Dickens"},{"Dimmit","Dimmit"},{"Donley","Donley"},{"Duval","Duval"},{"Eastland","Eastland"},
  {"Ector","Ector"},{"Edwards","Edwards"},{"El Paso","El Paso"},{"Ellis","Ellis"},{"Erath","Erath"},
  {"Falls","Falls"},{"Fannin","Fannin"},{"Fayette","Fayette"},{"Fisher","Fisher"},{"Floyd","Floyd"},
  {"Foard","Foard"},{"Fort Bend","Fort Bend"},{"Franklin","Franklin"},{"Freestone","Freestone"},
  {"Frio","Frio"},{"Gaines","Gaines"},{"Galveston","Galveston"},{"Garza","Garza"},{"Gillespie","Gillespie"},
  {"Glasscock","Glasscock"},{"Goliad","Goliad"},{"Gonzales","Gonzales"},{"Gray","Gray"},
  {"Grayson","Grayson"},{"Gregg","Gregg"},{"Grimes","Grimes"},{"Guadalupe","Guadalupe"},{"Hale","Hale"},
  {"Hall","Hall"},{"Hamilton","Hamilton"},{"Hansford","Hansford"},{"Hardeman","Hardeman"},
  {"Hardin","Hardin"},{"Harris","Harris"},{"Harrison","Harrison"},{"Hartley","Hartley"},
  {"Haskell","Haskell"},{"Hays","Hays"},{"Hemphill","Hemphill"},{"Henderson","Henderson"},
  {"Hidalgo","Hidalgo"},{"Hill","Hill"},{"Hockley","Hockley"},{"Hood","Hood"},{"Hopkins","Hopkins"},
  {"Houston","Houston"},{"Howard","Howard"},{"Hudspeth","Hudspeth"},{"Hunt","Hunt"},
  {"Hutchinson","Hutchinson"},{"Irion","Irion"},{"Jack","Jack"},{"Jackson","Jackson"},{"Jasper","Jasper"},
  {"Jeff Davis","Jeff Davis"},{"Jefferson","Jefferson"},{"Jim Hogg","Jim Hogg"},{"Jim Wells","Jim Wells"},
  {"Johnson","Johnson"},{"Jones","Jones"},{"Karnes","Karnes"},{"Kaufman","Kaufman"},{"Kendall","Kendall"},
  {"Kenedy","Kenedy"},{"Kent","Kent"},{"Kerr","Kerr"},{"Kimble","Kimble"},{"King","King"},
  {"Kinney","Kinney"},{"Kleberg","Kleberg"},{"Knox","Knox"},{"La Salle","La Salle"},{"Lamar","Lamar"},
  {"Lamb","Lamb"},{"Lampasas","Lampasas"},{"Lavaca","Lavaca"},{"Lee","Lee"},{"Leon","Leon"},
  {"Liberty","Liberty"},{"Limestone","Limestone"},{"Lipscomb","Lipscomb"},{"Live Oak","Live Oak"},
  {"Llano","Llano"},{"Loving","Loving"},{"Lubbock","Lubbock"},{"Lynn","Lynn"},{"Madison","Madison"},
  {"Marion","Marion"},{"Martin","Martin"},{"Mason","Mason"},{"Matagorda","Matagorda"},
  {"Maverick","Maverick"},{"McCulloch","McCulloch"},{"McLennan","McLennan"},{"McMullen","McMullen"},
  {"Medina","Medina"},{"Menard","Menard"},{"Midland","Midland"},{"Milam","Milam"},{"Mills","Mills"},
  {"Mitchell","Mitchell"},{"Montague","Montague"},{"Montgomery","Montgomery"},{"Moore","Moore"},
  {"Morris","Morris"},{"Motley","Motley"},{"Nacogdoches","Nacogdoches"},{"Navarro","Navarro"},
  {"Newton","Newton"},{"Nolan","Nolan"},{"Nueces","Nueces"},{"Ochiltree","Ochiltree"},{"Oldham","Oldham"},
  {"Orange","Orange"},{"Palo Pinto","Palo Pinto"},{"Panola","Panola"},{"Parker","Parker"},
  {"Parmer","Parmer"},{"Pecos","Pecos"},{"Polk","Polk"},{"Potter","Potter"},{"Presidio","Presidio"},
  {"Rains","Rains"},{"Randall","Randall"},{"Reagan","Reagan"},{"Real","Real"},{"Red River","Red River"},
  {"Reeves","Reeves"},{"Refugio","Refugio"},{"Roberts","Roberts"},{"Robertson","Robertson"},
  {"Rockwall","Rockwall"},{"Runnels","Runnels"},{"Rusk","Rusk"},{"Sabine","Sabine"},
  {"San Augustine","San Augustine"},{"San Jacinto","San Jacinto"},{"San Patricio","San Patricio"},
  {"San Saba","San Saba"},{"Schleicher","Schleicher"},{"Scurry","Scurry"},{"Shackelford","Shackelford"},
  {"Shelby","Shelby"},{"Sherman","Sherman"},{"Smith","Smith"},{"Somervell","Somervell"},{"Starr","Starr"},
  {"Stephens","Stephens"},{"Sterling","Sterling"},{"Stonewall","Stonewall"},{"Sutton","Sutton"},
  {"Swisher","Swisher"},{"Tarrant","Tarrant"},{"Taylor","Taylor"},{"Terrell","Terrell"},{"Terry","Terry"},
  {"Throckmorton","Throckmorton"},{"Titus","Titus"},{"Tom Green","Tom Green"},{"Travis","Travis"},
  {"Trinity","Trinity"},{"Tyler","Tyler"},{"Upshur","Upshur"},{"Upton","Upton"},{"Uvalde","Uvalde"},
  {"Val Verde","Val Verde"},{"Van Zandt","Van Zandt"},{"Victoria","Victoria"},{"Walker","Walker"},
  {"Waller","Waller"},{"Ward","Ward"},{"Washington","Washington"},{"Webb","Webb"},{"Wharton","Wharton"},
  {"Wheeler","Wheeler"},{"Wichita","Wichita"},{"Wilbarger","Wilbarger"},{"Willacy","Willacy"},
  {"Williamson","Williamson"},{"Wilson","Wilson"},{"Winkler","Winkler"},{"Wise","Wise"},{"Wood","Wood"},
  {"Yoakum","Yoakum"},{"Young","Young"},{"Zapata","Zapata"},{"Zavala","Zavala"},
};
static const int CNTY_TX_N = 254;

static const SubdivEntry CNTY_UT[] = {
  {"Beaver","Beaver"},{"Box Elder","Box Elder"},{"Cache","Cache"},{"Carbon","Carbon"},{"Daggett","Daggett"},
  {"Davis","Davis"},{"Duchesne","Duchesne"},{"Emery","Emery"},{"Garfield","Garfield"},{"Grand","Grand"},
  {"Iron","Iron"},{"Juab","Juab"},{"Kane","Kane"},{"Millard","Millard"},{"Morgan","Morgan"},
  {"Piute","Piute"},{"Rich","Rich"},{"Salt Lake","Salt Lake"},{"San Juan","San Juan"},{"Sanpete","Sanpete"},
  {"Sevier","Sevier"},{"Summit","Summit"},{"Tooele","Tooele"},{"Uintah","Uintah"},{"Utah","Utah"},
  {"Wasatch","Wasatch"},{"Washington","Washington"},{"Wayne","Wayne"},{"Weber","Weber"},
};
static const int CNTY_UT_N = 29;

static const SubdivEntry CNTY_VA[] = {
  {"Accomack","Accomack"},{"Albemarle","Albemarle"},{"Alexandria City","Alexandria City"},
  {"Alleghany","Alleghany"},{"Amelia","Amelia"},{"Amherst","Amherst"},{"Appomattox","Appomattox"},
  {"Arlington","Arlington"},{"Augusta","Augusta"},{"Bath","Bath"},{"Bedford","Bedford"},
  {"Bedford City","Bedford City"},{"Bland","Bland"},{"Botetourt","Botetourt"},{"Bristol","Bristol City"},
  {"Brunswick","Brunswick"},{"Buchanan","Buchanan"},{"Buckingham","Buckingham"},
  {"Buena Vista City","Buena Vista City"},{"Campbell","Campbell"},{"Caroline","Caroline"},
  {"Carroll","Carroll"},{"Charles City","Charles City"},{"Charlotte","Charlotte"},
  {"Charlottesville City","Charlottesville City"},{"Chesapeake City","Chesapeake City"},
  {"Chesterfield","Chesterfield"},{"Clarke","Clarke"},{"Clifton Forge City","Clifton Forge City"},
  {"Colonial Heights City","Colonial Heights City"},{"Covington City","Covington City"},{"Craig","Craig"},
  {"Culpeper","Culpeper"},{"Cumberland","Cumberland"},{"Danville City","Danville City"},
  {"Dickenson","Dickenson"},{"Dinwiddie","Dinwiddie"},{"Emporia City","Emporia City"},{"Essex","Essex"},
  {"Fairfax","Fairfax"},{"Fairfax City","Fairfax City"},{"Falls Church City","Falls Church City"},
  {"Fauquier","Fauquier"},{"Floyd","Floyd"},{"Fluvanna","Fluvanna"},{"Franklin","Franklin"},
  {"Franklin City","Franklin City"},{"Frederick","Frederick"},{"Fredericksburg City","Fredericksburg City"},
  {"Galax City","Galax City"},{"Giles","Giles"},{"Gloucester","Gloucester"},{"Goochland","Goochland"},
  {"Grayson","Grayson"},{"Greene","Greene"},{"Greensville","Greensville"},{"Halifax","Halifax"},
  {"Hampton City","Hampton City"},{"Hanover","Hanover"},{"Harrisonburg City","Harrisonburg City"},
  {"Henrico","Henrico"},{"Henry","Henry"},{"Highland","Highland"},{"Hopewell City","Hopewell City"},
  {"Isle of Wight","Isle of Wight"},{"James City","James City"},{"King and Queen","King and Queen"},
  {"King George","King George"},{"King William","King William"},{"Lancaster","Lancaster"},{"Lee","Lee"},
  {"Lexington City","Lexington City"},{"Loudoun","Loudoun"},{"Louisa","Louisa"},{"Lunenburg","Lunenburg"},
  {"Lynchburg City","Lynchburg City"},{"Madison","Madison"},{"Manassas City","Manassas City"},
  {"Manassas Park City","Manassas Park City"},{"Martinsville City","Martinsville City"},
  {"Mathews","Mathews"},{"Mecklenburg","Mecklenburg"},{"Middlesex","Middlesex"},{"Montgomery","Montgomery"},
  {"Nelson","Nelson"},{"New Kent","New Kent"},{"Newport News City","Newport News City"},
  {"Norfolk City","Norfolk City"},{"Northampton","Northampton"},{"Northumberland","Northumberland"},
  {"Norton City","Norton City"},{"Nottoway","Nottoway"},{"Orange","Orange"},{"Page","Page"},
  {"Patrick","Patrick"},{"Petersburg City","Petersburg City"},{"Pittsylvania","Pittsylvania"},
  {"Poquoson City","Poquoson City"},{"Portsmouth City","Portsmouth City"},{"Powhatan","Powhatan"},
  {"Prince Edward","Prince Edward"},{"Prince George","Prince George"},{"Prince William","Prince William"},
  {"Pulaski","Pulaski"},{"Radford City","Radford City"},{"Rappahannock","Rappahannock"},
  {"Richmond","Richmond"},{"Richmond City","Richmond City"},{"Roanoke","Roanoke"},
  {"Roanoke City","Roanoke City"},{"Rockbridge","Rockbridge"},{"Rockingham","Rockingham"},
  {"Russell","Russell"},{"Salem","Salem City"},{"Scott","Scott"},{"Shenandoah","Shenandoah"},
  {"Smyth","Smyth"},{"Southampton","Southampton"},{"Spotsylvania","Spotsylvania"},{"Stafford","Stafford"},
  {"Staunton City","Staunton City"},{"Suffolk City","Suffolk City"},{"Surry","Surry"},{"Sussex","Sussex"},
  {"Tazewell","Tazewell"},{"Virginia Beach City","Virginia Beach City"},{"Warren","Warren"},
  {"Washington","Washington"},{"Waynesboro City","Waynesboro City"},{"Westmoreland","Westmoreland"},
  {"Williamsburg City","Williamsburg City"},{"Winchester City","Winchester City"},{"Wise","Wise"},
  {"Wythe","Wythe"},{"York","York"},
};
static const int CNTY_VA_N = 135;

static const SubdivEntry CNTY_VT[] = {
  {"Addison","Addison"},{"Bennington","Bennington"},{"Caledonia","Caledonia"},{"Chittenden","Chittenden"},
  {"Essex","Essex"},{"Franklin","Franklin"},{"Grand Isle","Grand Isle"},{"Lamoille","Lamoille"},
  {"Orange","Orange"},{"Orleans","Orleans"},{"Rutland","Rutland"},{"Washington","Washington"},
  {"Windham","Windham"},{"Windsor","Windsor"},
};
static const int CNTY_VT_N = 14;

static const SubdivEntry CNTY_WA[] = {
  {"Adams","Adams"},{"Asotin","Asotin"},{"Benton","Benton"},{"Chelan","Chelan"},{"Clallam","Clallam"},
  {"Clark","Clark"},{"Columbia","Columbia"},{"Cowlitz","Cowlitz"},{"Douglas","Douglas"},{"Ferry","Ferry"},
  {"Franklin","Franklin"},{"Garfield","Garfield"},{"Grant","Grant"},{"Grays Harbor","Grays Harbor"},
  {"Island","Island"},{"Jefferson","Jefferson"},{"King","King"},{"Kitsap","Kitsap"},{"Kittitas","Kittitas"},
  {"Klickitat","Klickitat"},{"Lewis","Lewis"},{"Lincoln","Lincoln"},{"Mason","Mason"},
  {"Okanogan","Okanogan"},{"Pacific","Pacific"},{"Pend Oreille","Pend Oreille"},{"Pierce","Pierce"},
  {"San Juan","San Juan"},{"Skagit","Skagit"},{"Skamania","Skamania"},{"Snohomish","Snohomish"},
  {"Spokane","Spokane"},{"Stevens","Stevens"},{"Thurston","Thurston"},{"Wahkiakum","Wahkiakum"},
  {"Walla Walla","Walla Walla"},{"Whatcom","Whatcom"},{"Whitman","Whitman"},{"Yakima","Yakima"},
};
static const int CNTY_WA_N = 39;

static const SubdivEntry CNTY_WI[] = {
  {"Adams","Adams"},{"Ashland","Ashland"},{"Barron","Barron"},{"Bayfield","Bayfield"},{"Brown","Brown"},
  {"Buffalo","Buffalo"},{"Burnett","Burnett"},{"Calumet","Calumet"},{"Chippewa","Chippewa"},
  {"Clark","Clark"},{"Columbia","Columbia"},{"Crawford","Crawford"},{"Dane","Dane"},{"Dodge","Dodge"},
  {"Door","Door"},{"Douglas","Douglas"},{"Dunn","Dunn"},{"Eau Claire","Eau Claire"},{"Florence","Florence"},
  {"Fond du Lac","Fond du Lac"},{"Forest","Forest"},{"Grant","Grant"},{"Green","Green"},
  {"Green Lake","Green Lake"},{"Iowa","Iowa"},{"Iron","Iron"},{"Jackson","Jackson"},
  {"Jefferson","Jefferson"},{"Juneau","Juneau"},{"Kenosha","Kenosha"},{"Kewaunee","Kewaunee"},
  {"La Crosse","La Crosse"},{"Lafayette","Lafayette"},{"Langlade","Langlade"},{"Lincoln","Lincoln"},
  {"Manitowoc","Manitowoc"},{"Marathon","Marathon"},{"Marinette","Marinette"},{"Marquette","Marquette"},
  {"Menominee","Menominee"},{"Milwaukee","Milwaukee"},{"Monroe","Monroe"},{"Oconto","Oconto"},
  {"Oneida","Oneida"},{"Outagamie","Outagamie"},{"Ozaukee","Ozaukee"},{"Pepin","Pepin"},{"Pierce","Pierce"},
  {"Polk","Polk"},{"Portage","Portage"},{"Price","Price"},{"Racine","Racine"},{"Richland","Richland"},
  {"Rock","Rock"},{"Rusk","Rusk"},{"Saint Croix","St. Croix"},{"Sauk","Sauk"},{"Sawyer","Sawyer"},
  {"Shawano","Shawano"},{"Sheboygan","Sheboygan"},{"Taylor","Taylor"},{"Trempealeau","Trempealeau"},
  {"Vernon","Vernon"},{"Vilas","Vilas"},{"Walworth","Walworth"},{"Washburn","Washburn"},
  {"Washington","Washington"},{"Waukesha","Waukesha"},{"Waupaca","Waupaca"},{"Waushara","Waushara"},
  {"Winnebago","Winnebago"},{"Wood","Wood"},
};
static const int CNTY_WI_N = 72;

static const SubdivEntry CNTY_WV[] = {
  {"Barbour","Barbour"},{"Berkeley","Berkeley"},{"Boone","Boone"},{"Braxton","Braxton"},{"Brooke","Brooke"},
  {"Cabell","Cabell"},{"Calhoun","Calhoun"},{"Clay","Clay"},{"Doddridge","Doddridge"},{"Fayette","Fayette"},
  {"Gilmer","Gilmer"},{"Grant","Grant"},{"Greenbrier","Greenbrier"},{"Hampshire","Hampshire"},
  {"Hancock","Hancock"},{"Hardy","Hardy"},{"Harrison","Harrison"},{"Jackson","Jackson"},
  {"Jefferson","Jefferson"},{"Kanawha","Kanawha"},{"Lewis","Lewis"},{"Lincoln","Lincoln"},{"Logan","Logan"},
  {"Marion","Marion"},{"Marshall","Marshall"},{"Mason","Mason"},{"McDowell","McDowell"},{"Mercer","Mercer"},
  {"Mineral","Mineral"},{"Mingo","Mingo"},{"Monongalia","Monongalia"},{"Monroe","Monroe"},
  {"Morgan","Morgan"},{"Nicholas","Nicholas"},{"Ohio","Ohio"},{"Pendleton","Pendleton"},
  {"Pleasants","Pleasants"},{"Pocahontas","Pocahontas"},{"Preston","Preston"},{"Putnam","Putnam"},
  {"Raleigh","Raleigh"},{"Randolph","Randolph"},{"Ritchie","Ritchie"},{"Roane","Roane"},
  {"Summers","Summers"},{"Taylor","Taylor"},{"Tucker","Tucker"},{"Tyler","Tyler"},{"Upshur","Upshur"},
  {"Wayne","Wayne"},{"Webster","Webster"},{"Wetzel","Wetzel"},{"Wirt","Wirt"},{"Wood","Wood"},
  {"Wyoming","Wyoming"},
};
static const int CNTY_WV_N = 55;

static const SubdivEntry CNTY_WY[] = {
  {"Albany","Albany"},{"Big Horn","Big Horn"},{"Campbell","Campbell"},{"Carbon","Carbon"},
  {"Converse","Converse"},{"Crook","Crook"},{"Fremont","Fremont"},{"Goshen","Goshen"},
  {"Hot Springs","Hot Springs"},{"Johnson","Johnson"},{"Laramie","Laramie"},{"Lincoln","Lincoln"},
  {"Natrona","Natrona"},{"Niobrara","Niobrara"},{"Park","Park"},{"Platte","Platte"},{"Sheridan","Sheridan"},
  {"Sublette","Sublette"},{"Sweetwater","Sweetwater"},{"Teton","Teton"},{"Uinta","Uinta"},
  {"Washakie","Washakie"},{"Weston","Weston"},
};
static const int CNTY_WY_N = 23;

struct CountyIndex { const char* state; const SubdivEntry* list; int n; };
static const CountyIndex US_COUNTY_INDEX[] = {
  {"AK", CNTY_AK, CNTY_AK_N},
  {"AL", CNTY_AL, CNTY_AL_N},
  {"AR", CNTY_AR, CNTY_AR_N},
  {"AZ", CNTY_AZ, CNTY_AZ_N},
  {"CA", CNTY_CA, CNTY_CA_N},
  {"CO", CNTY_CO, CNTY_CO_N},
  {"CT", CNTY_CT, CNTY_CT_N},
  {"DE", CNTY_DE, CNTY_DE_N},
  {"FL", CNTY_FL, CNTY_FL_N},
  {"GA", CNTY_GA, CNTY_GA_N},
  {"HI", CNTY_HI, CNTY_HI_N},
  {"IA", CNTY_IA, CNTY_IA_N},
  {"ID", CNTY_ID, CNTY_ID_N},
  {"IL", CNTY_IL, CNTY_IL_N},
  {"IN", CNTY_IN, CNTY_IN_N},
  {"KS", CNTY_KS, CNTY_KS_N},
  {"KY", CNTY_KY, CNTY_KY_N},
  {"LA", CNTY_LA, CNTY_LA_N},
  {"MA", CNTY_MA, CNTY_MA_N},
  {"MD", CNTY_MD, CNTY_MD_N},
  {"ME", CNTY_ME, CNTY_ME_N},
  {"MI", CNTY_MI, CNTY_MI_N},
  {"MN", CNTY_MN, CNTY_MN_N},
  {"MO", CNTY_MO, CNTY_MO_N},
  {"MS", CNTY_MS, CNTY_MS_N},
  {"MT", CNTY_MT, CNTY_MT_N},
  {"NC", CNTY_NC, CNTY_NC_N},
  {"ND", CNTY_ND, CNTY_ND_N},
  {"NE", CNTY_NE, CNTY_NE_N},
  {"NH", CNTY_NH, CNTY_NH_N},
  {"NJ", CNTY_NJ, CNTY_NJ_N},
  {"NM", CNTY_NM, CNTY_NM_N},
  {"NV", CNTY_NV, CNTY_NV_N},
  {"NY", CNTY_NY, CNTY_NY_N},
  {"OH", CNTY_OH, CNTY_OH_N},
  {"OK", CNTY_OK, CNTY_OK_N},
  {"OR", CNTY_OR, CNTY_OR_N},
  {"PA", CNTY_PA, CNTY_PA_N},
  {"RI", CNTY_RI, CNTY_RI_N},
  {"SC", CNTY_SC, CNTY_SC_N},
  {"SD", CNTY_SD, CNTY_SD_N},
  {"TN", CNTY_TN, CNTY_TN_N},
  {"TX", CNTY_TX, CNTY_TX_N},
  {"UT", CNTY_UT, CNTY_UT_N},
  {"VA", CNTY_VA, CNTY_VA_N},
  {"VT", CNTY_VT, CNTY_VT_N},
  {"WA", CNTY_WA, CNTY_WA_N},
  {"WI", CNTY_WI, CNTY_WI_N},
  {"WV", CNTY_WV, CNTY_WV_N},
  {"WY", CNTY_WY, CNTY_WY_N},
};
static const int US_COUNTY_INDEX_N = 50;
