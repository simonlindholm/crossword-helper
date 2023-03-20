#!/usr/bin/env python3
import time
import cgi
import cgitb
import subprocess
import sys
import random

# cgi tracebacks
cgitb.enable()

title = random.choice("""
Artiga hamnat
Hitta magarna
Hitta gamarna
Hitta anagram
Magihattarna
Antagit hamra
Intaga Martha
Hariga mattan
Taniga Martha
Inhamrat gata
Taigan Martha
Hattiga manar
Inmatat hagar
Antagit harma
Timat hagarna
Itagna Martha
Hamrat intaga
Hamrat taniga
Hamra tantiga
Inhamrat taga
Gamahittarna
Hamrat taigan
Hattiga maran
Harmat intaga
Harmat taniga
Agnar thaimat
Hamrat itagna
Hatarna matig
Argan thaimat
Harmat taigan
Agat inhamrat
Harma tantiga
Ragan thaimat
Marthan taiga
Harmat itagna
""".strip().split("\n"))

print("Content-Type: text/html; charset=utf-8")
print()
print("""
<!DOCTYPE html>
<html>
<head>
<style>
* {
    box-sizing: border-box;
}
</style>
<head>
<body>
<form action="" method="get">
<input name="word" autofocus><br>
Max antal ord: <input type="number" name="max_words" value="2"><br>
Max antal resultat: <input type="number" name="limit" value="1000"><br>
<input type="submit" value='""" + title + """!'>
</form>
<br>
<pre><plaintext>""")
sys.stdout.flush()

form = cgi.FieldStorage()
if "word" in form:
    word = form["word"].value
    try:
        max_words = int(form["max_words"].value)
        limit = int(form["limit"].value)
        cmd = ["/home/protected/anagrams", "--max-words", str(max_words), "--limit", str(limit), "--", word]
        t0 = time.time()
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=4, cwd="/home/protected")
        if res.returncode != 0:
            print(f"Misslyckades med att hitta anagram (statuskod {res.returncode})")
        output = res.stdout.decode('utf-8', 'replace')
        print(output)
        hits = output.count('\n') if output else 0
        if "<reached limit" in output:
            hits = limit
        dur = time.time() - t0
        print(f"{hits} träffar, {dur:.3f}s.")
    except ValueError:
        print("not a number!")
