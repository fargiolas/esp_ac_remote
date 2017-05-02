package fargiolas.airconremote;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;

import static java.lang.Integer.max;
import static java.lang.Integer.min;

public class AirConState {
    private SharedPreferences prefs;
    private SharedPreferences.Editor editor;

    String[] ACModes = {"auto", "cool", "dry", "fan", "warm"};
    int ACMode = 0;
    String[] FanModes = {"auto", "low", "mid", "high"};
    int FanMode = 0;
    boolean PowerMode = false;
    boolean EnergyMode = false;
    boolean DisplayMode = false;
    int Temperature = 22;

    public AirConState(Activity a) {
        prefs = a.getPreferences(Context.MODE_PRIVATE);
        editor = prefs.edit();
    }

    private int index_of(String[] a, String s) {
        int i;

        for (i = 0; i < a.length; i++)
            if (a[i].equals(s)) return i;

        return -1;
    }
    private String bool2str(boolean v) {
        if (v) return "on";
        else return "off";
    }

    public String get_ACMode() {
        return prefs.getString("mode", ACModes[0]);
    }

    public String switch_ACMode() {
        String m = prefs.getString("mode", ACModes[0]);
        int i = index_of(ACModes, m);

        i = (i + 1) % ACModes.length;

        editor.putString("mode", ACModes[i]);
        editor.commit();

        return ACModes[i];
    }

    public String get_FanMode() {
        return prefs.getString("fan", FanModes[0]);
    }

    public String switch_FanMode() {
        String m = prefs.getString("fan", FanModes[0]);
        int i = index_of(FanModes, m);

        i = (i + 1) % FanModes.length;

        editor.putString("fan", FanModes[i]);
        editor.commit();

        return FanModes[i];
    }

    public String get_BoolModeStr(String tag) {
        return bool2str(prefs.getBoolean(tag, false));
    }
    public boolean get_BoolMode(String tag) {
        return prefs.getBoolean(tag, false);
    }
    public String set_BoolMode(String tag, boolean value) {
        editor.putBoolean(tag, value);
        editor.commit();

        return bool2str(value);
    }

    public int get_Temperature() {
        return prefs.getInt("temperature", 26);
    }
    public int set_Temperature(int T) {
        int temp = max(18, min(T, 30));

        editor.putInt("temperature", temp);
        editor.commit();

        return temp;
    }
}

