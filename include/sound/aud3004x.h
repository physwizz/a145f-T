#ifndef __SOUND_AUD3004X_H__
#define __SOUND_AUD3004X_H__

#if IS_ENABLED(CONFIG_SND_SOC_AUD3004X_5PIN) || IS_ENABLED(CONFIG_SND_SOC_AUD3004X_6PIN)
extern bool aud3004x_notifier_flag(void);
extern void aud3004x_call_notifier(u8 irq_codec[], int count);
#endif
#endif /* __SOUND_AUD3004X_H__ */
