#ifdef REC98_PORT
// REC98_PORT: bomb.hpp declares this `uint32_t`; see bomb.cpp's bomb_frame
// for why `unsigned long` (64-bit on this host's LP64 ABI) no longer
// matches on its own.
bool16 bomb_deals_damage(uint32_t frame)
#else
bool16 bomb_deals_damage(unsigned long frame)
#endif
{
	if((rem_lives == 0) && (bomb_damaging == true) && ((frame % 50) == 0)) {
		return true;
	}
	return false;
}
