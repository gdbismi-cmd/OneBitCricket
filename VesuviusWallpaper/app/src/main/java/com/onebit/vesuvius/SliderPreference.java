package com.onebit.vesuvius;

import android.content.Context;
import android.content.res.TypedArray;
import android.preference.Preference;
import android.util.AttributeSet;
import android.view.View;
import android.widget.SeekBar;
import android.widget.TextView;

import java.util.Locale;

/** A preference with an inline SeekBar that stores an int. */
public class SliderPreference extends Preference implements SeekBar.OnSeekBarChangeListener {
    private static final int PERCENT = 0, HOUR = 1, MINUTES = 2, COUNT = 3, WIND = 4;

    private final int min, max, display;
    private int value;
    private TextView valueView;

    public SliderPreference(Context context, AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.obtainStyledAttributes(attrs, R.styleable.SliderPreference);
        min = a.getInt(R.styleable.SliderPreference_min, 0);
        max = a.getInt(R.styleable.SliderPreference_max, 100);
        display = a.getInt(R.styleable.SliderPreference_display, PERCENT);
        a.recycle();
        setLayoutResource(R.layout.pref_slider);
    }

    @Override
    protected Object onGetDefaultValue(TypedArray a, int index) {
        return a.getInt(index, min);
    }

    @Override
    protected void onSetInitialValue(boolean restore, Object def) {
        value = restore ? getPersistedInt(value) : (Integer) def;
        if (!restore) persistInt(value);
    }

    @Override
    protected void onBindView(View view) {
        super.onBindView(view);
        SeekBar bar = view.findViewById(R.id.slider);
        valueView = view.findViewById(R.id.slider_value);
        bar.setOnSeekBarChangeListener(null);
        bar.setMax(max - min);
        bar.setProgress(value - min);
        bar.setOnSeekBarChangeListener(this);
        TextView summary = view.findViewById(android.R.id.summary);
        summary.setVisibility(getSummary() == null ? View.GONE : View.VISIBLE);
        valueView.setText(format(value));
    }

    public void setValue(int v) {
        value = Math.max(min, Math.min(max, v));
        persistInt(value);
        notifyChanged();
    }

    private String format(int v) {
        switch (display) {
            case HOUR: {
                int h = v / 4, m = (v % 4) * 15;
                return String.format(Locale.US, "%02d:%02d", h % 24, m);
            }
            case MINUTES: return v + " min";
            case COUNT: return String.valueOf(v);
            case WIND: {
                int s = v - 50;
                if (Math.abs(s) < 4) return "Calm";
                return (s < 0 ? "← " : "") + Math.abs(s) * 2 + "%" + (s > 0 ? " →" : "");
            }
            default: return v + "%";
        }
    }

    @Override
    public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
        if (!fromUser) return;
        int v = progress + min;
        if (valueView != null) valueView.setText(format(v));
        if (v != value && callChangeListener(v)) {
            value = v;
            persistInt(v);
        }
    }

    @Override public void onStartTrackingTouch(SeekBar bar) {}
    @Override public void onStopTrackingTouch(SeekBar bar) {}
}
