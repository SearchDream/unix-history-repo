# include <pwd.h>
# include "sendmail.h"

SCCSID(@(#)savemail.c	3.30		%G%);

/*
**  SAVEMAIL -- Save mail on error
**
**	If the MailBack flag is set, mail it back to the originator
**	together with an error message; otherwise, just put it in
**	dead.letter in the user's home directory (if he exists on
**	this machine).
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		Saves the letter, by writing or mailing it back to the
**		sender, or by putting it in dead.letter in her home
**		directory.
*/

savemail()
{
	register struct passwd *pw;
	register FILE *xfile;
	char buf[MAXLINE+1];
	extern struct passwd *getpwnam();
	register char *p;
	extern char *ttypath();
	static int exclusive;
	typedef int (*fnptr)();
	ENVELOPE errenvelope;
	register ENVELOPE *ee;
	extern ENVELOPE *newenvelope();

	if (exclusive++)
		return;
	if (CurEnv->e_class <= PRI_JUNK)
	{
		if (Verbose)
			message(Arpa_Info, "Dumping junk mail");
		return;
	}
	ForceMail = TRUE;

	/*
	**  In the unhappy event we don't know who to return the mail
	**  to, make someone up.
	*/

	if (CurEnv->e_from.q_paddr == NULL)
	{
		if (parse("root", &CurEnv->e_from, 0) == NULL)
		{
			syserr("Cannot parse root!");
			ExitStat = EX_SOFTWARE;
			finis();
		}
	}
	CurEnv->e_to = NULL;

	/*
	**  If called from Eric Schmidt's network, do special mailback.
	**	Fundamentally, this is the mailback case except that
	**	it returns an OK exit status (assuming the return
	**	worked).
	**  Also, if the from address is not local, mail it back.
	*/

	if (BerkNet)
	{
		ExitStat = EX_OK;
		MailBack = TRUE;
	}
	if (!bitset(M_LOCAL, CurEnv->e_from.q_mailer->m_flags))
		MailBack = TRUE;

	/*
	**  If writing back, do it.
	**	If the user is still logged in on the same terminal,
	**	then write the error messages back to hir (sic).
	**	If not, set the MailBack flag so that it will get
	**	mailed back instead.
	*/

	if (WriteBack)
	{
		p = ttypath();
		if (p == NULL || freopen(p, "w", stdout) == NULL)
		{
			MailBack = TRUE;
			errno = 0;
		}
		else
		{
			(void) fflush(Xscript);
			xfile = fopen(Transcript, "r");
			if (xfile == NULL)
				syserr("Cannot open %s", Transcript);
			expand("$n", buf, &buf[sizeof buf - 1], CurEnv);
			printf("\r\nMessage from %s...\r\n", buf);
			printf("Errors occurred while sending mail; transcript follows:\r\n");
			while (fgets(buf, sizeof buf, xfile) != NULL && !ferror(stdout))
				fputs(buf, stdout);
			if (ferror(stdout))
				(void) syserr("savemail: stdout: write err");
			(void) fclose(xfile);
		}
	}

	/*
	**  If mailing back, do it.
	**	Throw away all further output.  Don't do aliases, since
	**	this could cause loops, e.g., if joe mails to x:joe,
	**	and for some reason the network for x: is down, then
	**	the response gets sent to x:joe, which gives a
	**	response, etc.  Also force the mail to be delivered
	**	even if a version of it has already been sent to the
	**	sender.
	*/

	if (MailBack)
	{
		if (returntosender("Unable to deliver mail", &CurEnv->e_from, TRUE) == 0)
			return;
	}

	/*
	**  Save the message in dead.letter.
	**	If we weren't mailing back, and the user is local, we
	**	should save the message in dead.letter so that the
	**	poor person doesn't have to type it over again --
	**	and we all know what poor typists programmers are.
	*/

	if (ArpaMode)
		return;
	p = NULL;
	if (CurEnv->e_from.q_mailer == LocalMailer)
	{
		if (CurEnv->e_from.q_home != NULL)
			p = CurEnv->e_from.q_home;
		else if ((pw = getpwnam(CurEnv->e_from.q_user)) != NULL)
			p = pw->pw_dir;
	}
	if (p == NULL)
	{
		syserr("Can't return mail to %s", CurEnv->e_from.q_paddr);
# ifdef DEBUG
		p = "/usr/tmp";
# else
		p = NULL;
# endif
	}
	if (p != NULL && TempFile != NULL)
	{
		auto ADDRESS *q;

		/* we have a home directory; open dead.letter */
		message(Arpa_Info, "Saving message in dead.letter");
		define('z', p);
		expand("$z/dead.letter", buf, &buf[sizeof buf - 1], CurEnv);
		CurEnv->e_to = buf;
		q = NULL;
		sendto(buf, -1, (ADDRESS *) NULL, &q);
		(void) deliver(q);
	}

	/* add terminator to writeback message */
	if (WriteBack)
		printf("-----\r\n");
}
/*
**  RETURNTOSENDER -- return a message to the sender with an error.
**
**	Parameters:
**		msg -- the explanatory message.
**		sendbody -- if TRUE, also send back the body of the
**			message; otherwise just send the header.
**
**	Returns:
**		zero -- if everything went ok.
**		else -- some error.
**
**	Side Effects:
**		Returns the current message to the sender via
**		mail.
*/

static char	*ErrorMessage;
static bool	SendBody;

returntosender(msg, returnto, sendbody)
	char *msg;
	ADDRESS *returnto;
	bool sendbody;
{
	ADDRESS to_addr;
	char buf[MAXNAME];
	register int i;
	extern putheader(), errbody();
	register ENVELOPE *ee;
	extern ENVELOPE *newenvelope();
	ENVELOPE errenvelope;

	NoAlias = TRUE;
	SendBody = sendbody;
	ee = newenvelope(&errenvelope);
	ee->e_puthdr = putheader;
	ee->e_putbody = errbody;
	addheader("date", "$b", ee);
	addheader("from", "$g (Mail Delivery Subsystem)", ee);
	addheader("to", returnto->q_paddr, ee);
	addheader("subject", msg, ee);

	/* fake up an address header for the from person */
	bmove((char *) returnto, (char *) &to_addr, sizeof to_addr);
	expand("$n", buf, &buf[sizeof buf - 1], CurEnv);
	if (parse(buf, &ee->e_from, -1) == NULL)
	{
		syserr("Can't parse myself!");
		ExitStat = EX_SOFTWARE;
		return (-1);
	}
	to_addr.q_next = NULL;
	to_addr.q_flags &= ~QDONTSEND;
	ee->e_sendqueue = &to_addr;

	/* push state into submessage */
	CurEnv = ee;
	define('f', "$n");
	define('x', "Mail Delivery Subsystem");

	/* actually deliver the error message */
	i = deliver(&to_addr);

	/* if the error message was "queued", make that happen */
	if (bitset(QQUEUEUP, to_addr.q_flags))
		queueup(ee);

	/* restore state */
	CurEnv = CurEnv->e_parent;

	if (i != 0)
	{
		syserr("Can't return mail to %s", to_addr.q_paddr);
		return (-1);
	}
	return (0);
}
/*
**  ERRHEADER -- Output the header for error mail.
**
**	Parameters:
**		xfile -- the transcript file.
**		fp -- the output file.
**
**	Returns:
**		none
**
**	Side Effects:
**		Outputs the header for an error message.
*/

errheader(fp, m)
	register FILE *fp;
	register struct mailer *m;
{
	/*
	**  Output header of error message.
	*/

	putheader(fp, m);
}
/*
**  ERRBODY -- output the body of an error message.
**
**	Typically this is a copy of the transcript plus a copy of the
**	original offending message.
**
**	Parameters:
**		xfile -- the transcript file.
**		fp -- the output file.
**		xdot -- if set, use the SMTP hidden dot algorithm.
**
**	Returns:
**		none
**
**	Side Effects:
**		Outputs the body of an error message.
*/

errbody(fp, m, xdot)
	register FILE *fp;
	register struct mailer *m;
	bool xdot;
{
	register FILE *xfile;
	char buf[MAXLINE];

	(void) fflush(stdout);
	if ((xfile = fopen(Transcript, "r")) == NULL)
		syserr("Cannot open %s", Transcript);
	errno = 0;

	/*
	**  Output transcript of errors
	*/

	fprintf(fp, "   ----- Transcript of session follows -----\n");
	(void) fflush(Xscript);
	while (fgets(buf, sizeof buf, xfile) != NULL)
		fputs(buf, fp);

	/*
	**  Output text of original message
	*/

	if (NoReturn)
		fprintf(fp, "\n   ----- Return message suppressed -----\n\n");
	else if (TempFile != NULL)
	{
		if (SendBody)
		{
			fprintf(fp, "\n   ----- Unsent message follows -----\n");
			(void) fflush(fp);
			putheader(fp, Mailer[1], CurEnv->e_parent);
			fprintf(fp, "\n");
			putbody(fp, Mailer[1], xdot);
		}
		else
		{
			fprintf(fp, "\n  ----- Message header follows -----\n");
			(void) fflush(fp);
			putheader(fp, Mailer[1]);
		}
	}
	else
		fprintf(fp, "\n  ----- No message was collected -----\n\n");

	/*
	**  Cleanup and exit
	*/

	(void) fclose(xfile);
	if (errno != 0)
		syserr("errbody: I/O error");
}
