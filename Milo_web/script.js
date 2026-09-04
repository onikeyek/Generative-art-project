console.log("SCRIPT.JS IS WORKING");

// ========================================
// SUPABASE
// ========================================
const SUPABASE_URL = "https://kyygrdbkgyujtqaogxjm.supabase.co";
const SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Imt5eWdyZGJrZ3l1anRxYW9neGptIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODc5OTYwMjEsImV4cCI6MjEwMzU3MjAyMX0.2fQcxOJrn46DuhgXt_R8oyix65q8z3elMxO9f_qGmTI";


let supabaseClient = null;

try {
  supabaseClient = window.supabase.createClient(SUPABASE_URL, SUPABASE_KEY);
  console.log("Supabase client created successfully");
} catch (e) {
  console.error("Supabase client creation failed:", e);
}

/* =====================================================
   MILO FACE SYSTEM
===================================================== */

const faces = [
  {
    name: "content", color: "#7a9480",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.42, w*0.09, h*0.38];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#071a0e" stroke="#7a9480" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#071a0e" stroke="#7a9480" stroke-width="1.5"/>
        <ellipse cx="${c1}" cy="${cy}" rx="${rx*0.55}" ry="${ry*0.55}" fill="#7a9480"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx*0.55}" ry="${ry*0.55}" fill="#7a9480"/>
        <ellipse cx="${c1-w*0.012}" cy="${cy-h*0.1}" rx="${w*0.02}" ry="${w*0.02}" fill="#b0c8b4"/>
        <ellipse cx="${c2-w*0.012}" cy="${cy-h*0.1}" rx="${w*0.02}" ry="${w*0.02}" fill="#b0c8b4"/>
        <path d="M${c1+rx} ${h*0.84} Q${w/2} ${h*0.99} ${c2-rx} ${h*0.84}" stroke="#7a9480" stroke-width="1.8" fill="none" stroke-linecap="round"/>`;
    }
  },
  {
    name: "warm", color: "#d4913a",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.52, w*0.09, h*0.30];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a0c00" stroke="#d4913a" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a0c00" stroke="#d4913a" stroke-width="1.5"/>
        <line x1="${c1-rx}" y1="${cy-ry-h*0.1}" x2="${c1+rx}" y2="${cy-ry-h*0.1}" stroke="#d4913a" stroke-width="1.4" stroke-linecap="round"/>
        <line x1="${c2-rx}" y1="${cy-ry-h*0.1}" x2="${c2+rx}" y2="${cy-ry-h*0.1}" stroke="#d4913a" stroke-width="1.4" stroke-linecap="round"/>
        <ellipse cx="${c1}" cy="${cy}" rx="${rx*0.5}" ry="${ry*0.5}" fill="#d4913a"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx*0.5}" ry="${ry*0.5}" fill="#d4913a"/>
        <path d="M${c1+rx} ${h*0.88} Q${w/2} ${h*0.78} ${c2-rx} ${h*0.88}" stroke="#d4913a" stroke-width="1.8" fill="none" stroke-linecap="round"/>`;
    }
  },
  {
    name: "stuffy", color: "#9b7fc4",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.44, w*0.09, h*0.38];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#0e0616" stroke="#9b7fc4" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#0e0616" stroke="#9b7fc4" stroke-width="1.5"/>
        <ellipse cx="${c1}" cy="${cy}" rx="${rx*0.55}" ry="${ry*0.55}" fill="#9b7fc4"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx*0.55}" ry="${ry*0.55}" fill="#9b7fc4"/>
        <path d="M${c1+rx} ${h*0.84} Q${w/2} ${h*0.72} ${c2-rx} ${h*0.84}" stroke="#9b7fc4" stroke-width="1.8" fill="none" stroke-linecap="round"/>
        <path d="M${w*0.03} ${h*0.7} Q${w*0.07} ${h*0.78} ${w*0.11} ${h*0.7} Q${w*0.15} ${h*0.78} ${w*0.19} ${h*0.7}" stroke="#9b7fc4" stroke-width="1.2" fill="none" stroke-linecap="round"/>
        <path d="M${w*0.81} ${h*0.7} Q${w*0.85} ${h*0.78} ${w*0.89} ${h*0.7} Q${w*0.93} ${h*0.78} ${w*0.97} ${h*0.7}" stroke="#9b7fc4" stroke-width="1.2" fill="none" stroke-linecap="round"/>`;
    }
  },
  {
    name: "alert", color: "#c45a5a",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.40, w*0.11, h*0.42];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a0000" stroke="#c45a5a" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a0000" stroke="#c45a5a" stroke-width="1.5"/>
        <ellipse cx="${c1}" cy="${cy}" rx="${rx*0.62}" ry="${ry*0.62}" fill="#c45a5a"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx*0.62}" ry="${ry*0.62}" fill="#c45a5a"/>
        <ellipse cx="${c1-w*0.018}" cy="${cy-h*0.1}" rx="${w*0.022}" ry="${w*0.022}" fill="#e8a0a0"/>
        <ellipse cx="${c2-w*0.018}" cy="${cy-h*0.1}" rx="${w*0.022}" ry="${w*0.022}" fill="#e8a0a0"/>
        <path d="M${c1+rx} ${h*0.88} Q${w/2} ${h*0.78} ${c2-rx} ${h*0.88}" stroke="#c45a5a" stroke-width="1.8" fill="none" stroke-linecap="round"/>`;
    }
  },
  {
    name: "stressed", color: "#c4a030",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.44, w*0.09, h*0.38];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a1200" stroke="#c4a030" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#1a1200" stroke="#c4a030" stroke-width="1.5"/>
        <ellipse cx="${c1}" cy="${cy}" rx="${rx*0.58}" ry="${ry*0.58}" fill="#c4a030"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx*0.58}" ry="${ry*0.58}" fill="#c4a030"/>
        <line x1="${c1-rx}" y1="${cy-ry-0.06*h}" x2="${c1+rx}" y2="${cy-ry+0.06*h}" stroke="#c4a030" stroke-width="1.4" stroke-linecap="round"/>
        <line x1="${c2-rx}" y1="${cy-ry+0.06*h}" x2="${c2+rx}" y2="${cy-ry-0.06*h}" stroke="#c4a030" stroke-width="1.4" stroke-linecap="round"/>
        <path d="M${c1+rx} ${h*0.87} Q${w/2} ${h*0.97} ${c2-rx} ${h*0.87}" stroke="#c4a030" stroke-width="1.8" fill="none" stroke-linecap="round"/>`;
    }
  },
  {
    name: "sleepy", color: "#6a9ec4",
    d(s, w, h) {
      const [c1, c2, cy, rx, ry] = [w*0.27, w*0.73, h*0.56, w*0.09, h*0.18];
      s.innerHTML = `
        <ellipse cx="${c1}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#030c1a" stroke="#6a9ec4" stroke-width="1.5"/>
        <ellipse cx="${c2}" cy="${cy}" rx="${rx}" ry="${ry}" fill="#030c1a" stroke="#6a9ec4" stroke-width="1.5"/>
        <path d="M${c1-rx} ${cy} Q${c1} ${cy-h*0.18} ${c1+rx} ${cy}" stroke="#6a9ec4" stroke-width="1" fill="none"/>
        <path d="M${c2-rx} ${cy} Q${c2} ${cy-h*0.18} ${c2+rx} ${cy}" stroke="#6a9ec4" stroke-width="1" fill="none"/>
        <ellipse cx="${c1}" cy="${cy+h*0.07}" rx="${rx*0.5}" ry="${ry*0.52}" fill="#6a9ec4" opacity=".5"/>
        <ellipse cx="${c2}" cy="${cy+h*0.07}" rx="${rx*0.5}" ry="${ry*0.52}" fill="#6a9ec4" opacity=".5"/>
        <path d="M${c1+rx} ${h*0.88} Q${w/2} ${h*0.96} ${c2-rx} ${h*0.88}" stroke="#6a9ec4" stroke-width="1.8" fill="none" stroke-linecap="round"/>
        <text x="${w*0.83}" y="${h*0.3}" font-size="${h*0.2}" fill="#6a9ec4" opacity=".4" font-family="monospace">z</text>
        <text x="${w*0.9}" y="${h*0.12}" font-size="${h*0.13}" fill="#6a9ec4" opacity=".22" font-family="monospace">z</text>`;
    }
  }
];

/* =====================================================
   FACE ANIMATION
===================================================== */

let fi = 0;

const heroSvg   = document.getElementById("heroSvg");
const specSvg   = document.getElementById("specSvg");
const statePill = document.getElementById("statePill");

faces.forEach((face, index) => {
  const el = document.getElementById("sf" + index);
  if (el) face.d(el, 72, 36);
});

if (heroSvg && specSvg && statePill) {
  faces[0].d(heroSvg, 164, 82);
  faces[0].d(specSvg, 122, 61);
  statePill.style.color = faces[0].color;
  statePill.style.borderColor = faces[0].color;

  setInterval(() => {
    fi = (fi + 1) % faces.length;
    const face = faces[fi];
    face.d(heroSvg, 164, 82);
    face.d(specSvg, 122, 61);
    statePill.textContent = face.name;
    statePill.style.color = face.color;
    statePill.style.borderColor = face.color;
  }, 2600);
}

/* =====================================================
   ROTATING HERO WORD
===================================================== */

const words   = ["face.", "voice.", "mood.", "mind."];
let wi        = 0;
const rotWord = document.getElementById("rotWord");

if (rotWord) {
  rotWord.style.transition = "opacity .28s";
  setInterval(() => {
    rotWord.style.opacity = "0";
    setTimeout(() => {
      wi = (wi + 1) % words.length;
      rotWord.textContent = words[wi];
      rotWord.style.opacity = "1";
    }, 290);
  }, 3400);
}

/* =====================================================
   MOBILE MENU
===================================================== */

const menuToggle  = document.getElementById("menuToggle");
const navLinks    = document.querySelector(".nlinks");
const navOverlay  = document.getElementById("navOverlay");

if (menuToggle && navLinks) {
  menuToggle.addEventListener("click", () => {
    const isOpen = navLinks.classList.toggle("active");
    menuToggle.classList.toggle("active", isOpen);
    if (navOverlay) navOverlay.classList.toggle("active", isOpen);
    document.body.classList.toggle("menu-open", isOpen);
  });

  document.querySelectorAll(".nlinks a").forEach(link => {
    link.addEventListener("click", () => {
      navLinks.classList.remove("active");
      menuToggle.classList.remove("active");
      if (navOverlay) navOverlay.classList.remove("active");
      document.body.classList.remove("menu-open");
    });
  });

  if (navOverlay) {
    navOverlay.addEventListener("click", () => {
      navLinks.classList.remove("active");
      menuToggle.classList.remove("active");
      navOverlay.classList.remove("active");
      document.body.classList.remove("menu-open");
    });
  }
}

/* =====================================================
   SCROLL REVEAL
===================================================== */

const observer = new IntersectionObserver(
  (entries) => {
    entries.forEach((entry) => {
      if (entry.isIntersecting) entry.target.classList.add("in");
    });
  },
  { threshold: 0.08 }
);

document.querySelectorAll(".rv").forEach((el) => observer.observe(el));

/* =====================================================
   WAITLIST
===================================================== */

async function joinWL() {
  console.log("JOIN WAITLIST FUNCTION STARTED");

  const emailInput    = document.getElementById("wlEmail");
  const errorMessage  = document.getElementById("wlErr");
  const successMessage= document.getElementById("wlOk");
  const button        = document.getElementById("joinWaitlistBtn");

  if (!emailInput) { console.error("Email input not found"); return; }

  const email = emailInput.value.trim();
  const emailPattern = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

  if (!email || !emailPattern.test(email)) {
    errorMessage.textContent = "Enter a valid email address.";
    errorMessage.style.display = "block";
    emailInput.style.borderColor = "#c45a5a";
    return;
  }

  errorMessage.style.display = "none";
  emailInput.style.borderColor = "";
  button.disabled = true;
  button.textContent = "Joining...";

  console.log("Attempting to save email:", email);

  try {
    const response = await fetch(`${SUPABASE_URL}/rest/v1/waitlist`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "apikey": SUPABASE_KEY,
        "Authorization": `Bearer ${SUPABASE_KEY}`,
        "Prefer": "return=minimal"
      },
      body: JSON.stringify({ email: email })
    });

    console.log("Response status:", response.status);

    if (response.ok || response.status === 201) {
      console.log("EMAIL SUCCESSFULLY SAVED");
      document.querySelector(".wl-row").style.display  = "none";
      document.querySelector(".wl-fine").style.display = "none";
      document.querySelector(".wl-note").style.display = "none";
      successMessage.style.display = "block";
    } else {
      const errBody = await response.text();
      console.error("Server error:", response.status, errBody);
      throw new Error(errBody);
    }

  } catch (err) {
    console.error("WAITLIST ERROR:", err);
    errorMessage.textContent = "Something went wrong. Please try again.";
    errorMessage.style.display = "block";
    button.disabled = false;
    button.textContent = "Join waitlist";
  }
}

const joinWaitlistBtn = document.getElementById("joinWaitlistBtn");
if (joinWaitlistBtn) {
  joinWaitlistBtn.addEventListener("click", () => {
    console.log("JOIN WAITLIST BUTTON CLICKED");
    joinWL();
  });
}

const waitlistEmail = document.getElementById("wlEmail");
if (waitlistEmail) {
  waitlistEmail.addEventListener("input", function () {
    this.style.borderColor = "";
    document.getElementById("wlErr").style.display = "none";
  });
}