import re # regex
import time # i forgot, probably for the time thingy on button 1
import psutil # most of the things (system stats, battery, etc.)
import configparser # handles .ini file
from ctypes import windll, create_unicode_buffer # get foreground window title
from pypresence import Presence # you know this don't u .-.
from win10toast import ToastNotifier # push notifs if errors / completes

# i call this massive update since i don't know anything abt configparser before XD
config = configparser.ConfigParser()
config.read('config.ini')

# don't forget to install the dependencies! >.>
# open https://discord.com/developers first, create new app, then copy the Application ID
client_id = config['general']['client_id']
RPC = Presence(client_id)
toaster = ToastNotifier()

# replace / censor words (good way to prevent your IRL name from showing, if that's what u want)
# oe, change the config ini file, you are not looking for anything here >.>
CENSOR_MAP = dict(config['censor_map'])

# push notif on Windows 10 if connected
def connect_rpc():
    is_shown = False
    restart_message = config['messages']['rpc_restarted_message']  # Customizable restarted message

    while True:
        try:
            RPC.connect()
            toaster.show_toast("Discord", restart_message)
            return True
        except Exception as e:
            if not is_shown:
                toaster.show_toast("Discord", f"{e}")
                is_shown = True
            time.sleep(10)

connect_rpc()

# detect opened programs
def get_focused_window_title():
    hwnd = windll.user32.GetForegroundWindow()
    buffer = create_unicode_buffer(128)
    windll.user32.GetWindowTextW(hwnd, buffer, len(buffer))
    idle_message = config['general'].get('idle_message')
    window_title = buffer.value or idle_message
    
    # if pattern replacement is applied on raw title, do that before anything else
    if config['censor_map'].getboolean('apply_pattern_on_raw'):
        pattern_replace_map = dict(item.split(" = ") for item in config['censor_map']['pattern_replace'].strip().splitlines() if item)
        for pattern, replacement in pattern_replace_map.items():
            window_title = re.sub(pattern, replacement, window_title)
        return window_title
    
    # apply censoring rules based on defined order
    rule_order = config['censor_map']['rule_order'].split(', ')
    for rule in rule_order:
        if rule == 'full_replace':
            full_replace_map = dict(item.split(" = ") for item in config['censor_map']['full_replace'].strip().splitlines() if item)
            for keyword, replacement in full_replace_map.items():
                if keyword.lower() in window_title.lower():
                    window_title = replacement
                    break
        
        elif rule == 'word_replace':
            word_replace_map = dict(item.split(" = ") for item in config['censor_map']['word_replace'].strip().splitlines() if item)
            for keyword, replacement in word_replace_map.items():
                window_title = window_title.replace(keyword, replacement)
        
        elif rule == 'pattern_replace':
            pattern_replace_map = {}
            for item in config['censor_map']['pattern_replace'].strip().splitlines():
                if " = " in item:
                    key, value = item.split(" = ", 1)  # Split with max 1 split
                    pattern_replace_map[key] = "" if value == "<empty>" else value
            for pattern, replacement in pattern_replace_map.items():
                window_title = re.sub(pattern, replacement, window_title)
    
    # handle single-char window titles, because discord is weird
    # (although arRPC bypasses this!)
    if len(window_title) == 1:
        window_title += " "
    
    return window_title

# CPU + RAM usage
def get_system_info():
    cpu_percent = psutil.cpu_percent()
    ram_percent = psutil.virtual_memory().percent
    total_ram_gb = round(psutil.virtual_memory().total / (1024 ** 3), 1)
    return cpu_percent, ram_percent, total_ram_gb

def format_template(template, time_str, battery_percent, plugged, time_remaining_str, hours=None, minutes=None, seconds=None):
    # Handle no battery case
    if battery_percent is None and plugged is None:
        replacements = {
            "<time>": time_str,
            "<percentage>": "",
            "<plug>": "🖥️",
            "<remaining>": ""
        }
    else:
        plug_symbol = "🔋" if not plugged else "🔌"
        replacements = {
            "<time>": time_str,
            "<percentage>": f"{battery_percent}%",
            "<plug>": plug_symbol,
            "<remaining>": time_remaining_str
        }

    # Add idle-specific placeholders if provided
    if hours is not None:
        replacements["<hours>"] = str(hours)
    if minutes is not None:
        replacements["<minutes>"] = str(minutes)
    if seconds is not None:
        replacements["<seconds>"] = str(seconds)

    # Replace placeholders in template
    for placeholder, value in replacements.items():
        template = template.replace(placeholder, value)

    return template.strip()

# 1st button: clock + battery
def get_time_and_battery():
    now = time.localtime()
    time_str = time.strftime("%I:%M %p", now)

    battery_info = psutil.sensors_battery()
    if battery_info is None:
        return time_str, None, None, "🖥️"  # No battery

    battery_percent = battery_info.percent
    plugged = battery_info.power_plugged
    
    time_remaining_str = ""
    if not plugged and battery_info.secsleft > 0:
        hours = battery_info.secsleft // 3600
        if hours <= 24:
            minutes = (battery_info.secsleft % 3600) // 60
            time_remaining_str = f" ({hours}h {minutes}m)"
    
    return time_str, battery_percent, plugged, time_remaining_str

# large picture
def get_large_image_and_text(time_str, battery_percent, plugged, time_remaining_str):
    now = time.localtime()
    hour = now.tm_hour
    
    # load time ranges from that ini file
    morning_start = int(config['large_time_ranges']['morning_start'])
    morning_end = int(config['large_time_ranges']['morning_end'])
    afternoon_start = int(config['large_time_ranges']['afternoon_start'])
    afternoon_end = int(config['large_time_ranges']['afternoon_end'])
    evening_start = int(config['large_time_ranges']['evening_start'])
    evening_end = int(config['large_time_ranges']['evening_end'])
    night_start = int(config['large_time_ranges']['night_start'])
    night_end = int(config['large_time_ranges']['night_end'])
    
    # morning
    if morning_start <= hour < morning_end:
        return (
            config['large_assets']['morning_image'],
            format_template(config['large_assets']['morning_text'], time_str, battery_percent, plugged, time_remaining_str)
        )
    
    # afternoon
    elif afternoon_start <= hour < afternoon_end:
        return (
            config['large_assets']['afternoon_image'],
            format_template(config['large_assets']['afternoon_text'], time_str, battery_percent, plugged, time_remaining_str)
        )
    
    # evening
    elif evening_start <= hour < evening_end:
        return (
            config['large_assets']['evening_image'],
            format_template(config['large_assets']['evening_text'], time_str, battery_percent, plugged, time_remaining_str)
        )
    
    # night
    elif night_start <= hour < night_end:
        return (
            config['large_assets']['night_image'],
            format_template(config['large_assets']['night_text'], time_str, battery_percent, plugged, time_remaining_str)
        )
    
    # default
    else:
        return (
            config['large_assets']['default_image'],
            format_template(config['large_assets']['default_text'], time_str, battery_percent, plugged, time_remaining_str)
        )

# small picture - i also made it react if AFK more than 2 minutes
def get_small_image_and_text(idle_time, time_str, battery_percent, plugged, time_remaining_str):
    idle_threshold = int(config['afk']['idle_threshold'])
    afk_message = config['afk']['afk_message']
    afk_image = config['afk'].get('afk_image', 'zzz')  # fallback to zzz if not set
    show_idle_time = config.getboolean('afk', 'show_idle_time')

    # AFK logic
    if idle_time >= idle_threshold or last_window_title == "Windows Default Lock Screen":
        idle_str = ""
        if show_idle_time:
            hours_idle = int(idle_time // 3600)
            minutes_idle = int((idle_time % 3600) // 60)
            seconds_idle = int(idle_time % 60)

            # Get idle_text template from config
            idle_template = config['afk'].get('idle_text', "since <hours>h <minutes>m <seconds>s")

            # Format with both idle + global placeholders
            idle_str = format_template(idle_template, time_str, battery_percent, plugged, time_remaining_str,
                                       hours=hours_idle, minutes=minutes_idle, seconds=seconds_idle)

        # Apply AFK message with idle_str
        afk_text = format_template(f"{afk_message} {idle_str}".strip(), time_str, battery_percent, plugged, time_remaining_str,
                                   hours=hours_idle if show_idle_time else None,
                                   minutes=minutes_idle if show_idle_time else None,
                                   seconds=seconds_idle if show_idle_time else None)

        return afk_image, afk_text

    # Load configurable time ranges
    morning_start = int(config['small_time_ranges']['morning_start'])
    morning_end = int(config['small_time_ranges']['morning_end'])
    afternoon_start = int(config['small_time_ranges']['afternoon_start'])
    afternoon_end = int(config['small_time_ranges']['afternoon_end'])
    evening_start = int(config['small_time_ranges']['evening_start'])
    evening_end = int(config['small_time_ranges']['evening_end'])
    night_start = int(config['small_time_ranges']['night_start'])
    night_end = int(config['small_time_ranges']['night_end'])

    # Current hour
    now = time.localtime()
    hour = now.tm_hour

    # Choose range
    if morning_start <= hour < morning_end:
        image = config['small_assets']['morning_image']
        text = config['small_assets']['morning_text']
    elif afternoon_start <= hour < afternoon_end:
        image = config['small_assets']['afternoon_image']
        text = config['small_assets']['afternoon_text']
    elif evening_start <= hour < evening_end:
        image = config['small_assets']['evening_image']
        text = config['small_assets']['evening_text']
    elif night_start <= hour < night_end:
        image = config['small_assets']['night_image']
        text = config['small_assets']['night_text']
    else:
        image = config['small_assets']['default_image']
        text = config['small_assets']['default_text']

    text = format_template(text, time_str, battery_percent, plugged, time_remaining_str)
    return image, text

# elapsed time, this is your system's uptime
def get_system_uptime_start():
    return int(psutil.boot_time())

# main logic - don't change anything!
def update_presence():
    global last_window_title, idle_start_time
    actual_window_title = get_focused_window_title()
    window_title_detection_enabled = config.getboolean('general', 'window_title_detection_enabled')
    
    if window_title_detection_enabled:
        window_title = actual_window_title
    else:
        window_title = config['general']['fallback_details']
    
    if actual_window_title == last_window_title:
        idle_time = time.time() - idle_start_time
    else:
        idle_time = 0 if last_window_title != "Windows Default Lock Screen" else time.time() - idle_start_time
        idle_start_time = time.time() if last_window_title != "Windows Default Lock Screen" else idle_start_time
        last_window_title = actual_window_title
    
    cpu_percent, ram_percent, total_ram_gb = get_system_info()
    time_str, battery_percent, plugged, time_remaining_str = get_time_and_battery()
    large_image, large_text = get_large_image_and_text(time_str, battery_percent, plugged, time_remaining_str)
    small_image, small_text = get_small_image_and_text(idle_time, time_str, battery_percent, plugged, time_remaining_str)
    system_uptime_start = get_system_uptime_start()

    # on/off switch - WARNING: this is unfinished
    show_large_image = config.getboolean('layout', 'show_large_image')
    show_small_image = config.getboolean('layout', 'show_small_image')
    show_buttons = config.getboolean('layout', 'show_buttons')
    show_details = config.getboolean('layout', 'details_field')
    show_state = config.getboolean('layout', 'state_field')
    
    # Button configs
    button_1_label = config['buttons']['button_1_label']
    button_1_url = config['buttons']['button_1_url']
    button_2_label = config['buttons']['button_2_label']
    button_2_url = config['buttons']['button_2_url']

    # Apply formatting
    button_1_label = format_template(config['buttons']['button_1_label'], time_str, battery_percent, plugged, time_remaining_str)
    button_2_label = format_template(config['buttons']['button_2_label'], time_str, battery_percent, plugged, time_remaining_str)

    # button logic
    buttons = []
    if show_buttons:
        buttons = [
            {"label": button_1_label, "url": button_1_url},
            {"label": button_2_label, "url": button_2_url}
        ]
    
    # other logic
    presence_data = {}
    
    if show_details:
        presence_data['details'] = window_title
    
    if show_state:
        presence_data['state'] = f"CPU: {cpu_percent}%, RAM: {ram_percent}% (of {total_ram_gb}GB)"
    
    presence_data['start'] = system_uptime_start
    
    if show_large_image:
        presence_data['large_image'] = large_image
        presence_data['large_text'] = large_text
    
    if show_small_image:
        presence_data['small_image'] = small_image
        presence_data['small_text'] = small_text
    
    if show_buttons:
        presence_data['buttons'] = buttons
    
    # push
    try:
        RPC.update(**presence_data)
    except Exception as e:
        toaster.show_toast("Discord", f"{e}")
        RPC.close()
        connect_rpc()

# AFK indicator logic
last_window_title = ""
idle_start_time = time.time()

# updates every 5 seconds, i don't recommend to change it lower than that!
# (if your laptop / PC can't handle it, it will probably lag the entire system, yk windows xD)
while True:
    update_presence()
    update_interval = int(config['general'].get('update_interval', 5))  # Default to 5 seconds if not set
    time.sleep(update_interval)
