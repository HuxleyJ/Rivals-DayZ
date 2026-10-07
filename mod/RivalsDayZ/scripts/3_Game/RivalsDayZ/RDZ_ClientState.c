// Client-side state the HUD (5_Mission) reads; filled from 4_World code and RPCs.

class RDZ_ClientState
{
	static ref array<string> s_Toasts = new array<string>;
	static string s_Invite;

	static void Toast(string text)
	{
		s_Toasts.Insert(text);
		RDZ.Log(text);
	}

	static string PopToast()
	{
		if (s_Toasts.Count() == 0)
			return "";
		string t = s_Toasts.Get(0);
		s_Toasts.RemoveOrdered(0);
		return t;
	}
}
