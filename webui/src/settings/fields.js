const F=(g,w,l,h,x)=>Object.assign({g,w,l,h},x);
const BRI_H='Ignored while auto brightness is on.';
const HHELP='Global text color or a color of its own.';
const OVERLAYS=['rain','snow','drizzle','storm','thunder','frost'];
const NULLABLE={colorCorrection:'#FFFFFF',colorTint:'#FFFFFF'};
const FIELDS={
 brightness:F('bright','slider','Brightness',BRI_H,{min:0,max:255}),
 autoBrightness:F('bright','toggle','Auto brightness','Follows the ambient light sensor.'),
 power:F('bright','toggle','Display on','Turns the matrix off without cutting power.'),
 appDurationMs:F('apps','number','Time per app','How long each app stays.',{min:1000,max:3600000,step:500,unit:'ms'}),
 autoTransition:F('apps','toggle','Auto rotation','Moves to the next app on its own.'),
 transitionDurationMs:F('apps','number','Transition duration','Length of the switch. 0 = instant.',{min:0,max:5000,unit:'ms'}),
 transitionEffect:F('apps','select','Transition effect','Animation between two apps.',{opt:'transitions'}),
 transitionDirection:F('apps','select','Transition direction','Reverses directional effects.',{opt:[
   ['normal','Normal'],['reverse','Reverse']]}),
 blockNavigation:F('apps','toggle','Block buttons','Buttons stop switching apps.'),
 textColor:F('text','color','Text color','Applies to all text.'),
 uppercase:F('text','toggle','Uppercase','All text in capitals.'),
 //linux:begin
 enlargeApps:F('hidden','toggle','Enlarge apps','Pushed apps and notifications made for 8 rows fill the panel at double size.'),
 //linux:end
 'scroll.mode':F('text','select','Scroll mode','What text does when it is too long for the panel.',{opt:[
   ['static','Stand still, cut off the rest'],
   ['wrap','Start over after each pass'],
   ['loop','Loop continuously'],
   ['bounce','Bounce back and forth']]}),
 'scroll.direction':F('text','select','Scroll direction','Which way the text moves.',{opt:[
   ['left','Right to left'],
   ['right','Left to right']]}),
 'scroll.entry':F('text','select','Text entry','Where a text starts its first pass.',{opt:[
   ['inline','Visible right away'],
   ['offscreen','Scroll in from the edge']]}),
 'scroll.whenFits':F('text','select','Text that fits','For text short enough for the panel.',{opt:[
   ['static','Stand still'],
   ['scroll','Scroll anyway']]}),
 'scroll.speed':F('text','slider','Scroll speed','100 % = normal, 0 stops.',{min:0,max:200,unit:'%'}),
 'scroll.gap':F('text','slider','Loop gap','Space between repetitions.',{min:0,max:64,unit:'px'}),
 'scroll.holdMs':F('text','slider','Pause','Before scrolling starts and at each bounce.',{min:0,max:5000,step:100,unit:'ms'}),
 overlay:F('overlay','select','Overlay','Not linked to real weather.',{opt:[['','None']].concat(OVERLAYS.map(o=>[o,o[0].toUpperCase()+o.slice(1)]))}),
 overlaySpeed:F('overlay','slider','Overlay speed','100 % = normal speed.',{min:10,max:200,step:5,unit:'%'}),
 saturation:F('color','slider','Saturation','0 % = grayscale.',{min:0,max:100,unit:'%'}),
 gamma:F('color','number','Gamma','Higher = darker mid-tones.',{min:1,max:3,step:0.1}),
 colorCorrection:F('color','color','Color correction','Fixes a color cast. White = off.'),
 colorTint:F('color','color','Color tint','Tints the whole display. White = off.'),
};
const APPF={
 timeMode:['Clock layout','What the clock app draws.',[
   [0,'Time only'],[1,'Calendar, weekday bar below'],
   [2,'Calendar, weekday bar on top'],[3,'Spiral calendar, weekday below'],
   [4,'Spiral calendar, weekday on top'],[5,'Big clock'],[6,'Binary clock']]],
 //linux:begin
 clockFace:['Clock face','What the clock app draws.',[
   ['sheet','Calendar sheet'],['ring','Ring calendar'],['flap','Split-flap'],
   ['month','Month sheet'],['big','Big clock']]],
 //linux:end
 time24h:['24-hour clock','Off = 12-hour clock.'],
 timeLeadingZero:['Leading zero','Show 07:05 instead of 7:05.'],
 timeShowSeconds:['Show seconds','Only in layouts with room for it.'],
 timeShowAmPm:['AM/PM indicator','12-hour clock only. Hidden while seconds show.'],
 timeSeparatorMode:['Colon between hours and minutes','Blinking shows the seconds without digits.',[
   ['steady','Steady on'],['blink','Blinking every second'],
   ['pulse','Pulsing (soft fade)']]],
 dateOrder:['Date order','Order of day, month and year.',[
   ['dayMonthYear','Day, month, year (31.12.25)'],
   ['monthDayYear','Month, day, year (12/31/25)'],
   ['yearMonthDay','Year, month, day (25-12-31)']]],
 dateSeparator:['Date separator','Character between day and month.',[
   ['dot','Dot (31.12.)'],['slash','Slash (31/12)'],
   ['dash','Dash (31-12)']]],
 dateYearMode:['Year in the date','Hiding it leaves more room on the panel.',[
   ['none','Hidden'],['twoDigit','2 digits (25)'],
   ['fourDigit','4 digits (2025)']]],
 dateShowWeekday:['Weekday prefix','Short weekday name before the date (Wed 31.12.).'],
 dateMonthNames:['Month as name','Show “31 Dec” instead of “31.12”.'],
 'weekdayBar.show':['Weekday bar','Seven marks, one per weekday.'],
 'weekdayBar.startOnMonday':['Week starts Monday','Display order only.'],
 'weekdayBar.weekendDays':['Weekend days','Days that use the weekend colors.'],
 'weekdayBar.activeColor':['Today','Mark of the current day.'],
 'weekdayBar.inactiveColor':['Other workdays','Marks of the remaining days.'],
 'weekdayBar.weekendActiveColor':['Today on a weekend','Used when today is a weekend day.'],
 'weekdayBar.weekendInactiveColor':['Other weekend days','The rest of the weekend list.'],
 timeColor:['Time color',HHELP],
 dateColor:['Date color',HHELP],
 calendarHeaderColor:['Calendar header','Top of the calendar. Some layouts also use it for weekends.'],
 calendarTextColor:['Calendar text','Day number.'],
 calendarBodyColor:['Calendar body','Calendar background.'],
 //linux:begin
 calendarAnimation:['Calendar animation','Tears the sheet off when the clock appears and at midnight.'],
 //linux:end
 useCelsius:['Celsius','Off = Fahrenheit.'],
 temperatureColor:['Temperature color',HHELP],
 humidityColor:['Humidity color',HHELP],
 batteryColor:['Battery color',HHELP],
};
const SET_GROUPS=[['bright','grpBright','grpBrightH'],['color','grpColor','grpColorH'],
 ['apps','grpApps','grpAppsH'],['text','grpText','grpTextH'],['overlay','grpOverlay','grpOverlayH']];
