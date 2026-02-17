/**
 * HTML overlay for Login / Register forms.
 * Sits on top of the Phaser canvas. Dark-themed to match the game.
 * Calls AuthClient under the hood and invokes a callback on success.
 */

import { AuthClient, AuthResponse } from '../systems/AuthClient.js';

export type LoginSuccessCallback = (result: AuthResponse) => void;

export class LoginOverlay {
  private container: HTMLDivElement;
  private mode: 'login' | 'register' = 'login';
  private onSuccess: LoginSuccessCallback;

  // Form elements
  private usernameInput!: HTMLInputElement;
  private passwordInput!: HTMLInputElement;
  private confirmInput!: HTMLInputElement;
  private confirmRow!: HTMLDivElement;
  private submitBtn!: HTMLButtonElement;
  private toggleLink!: HTMLAnchorElement;
  private errorDiv!: HTMLDivElement;
  private titleEl!: HTMLHeadingElement;

  constructor(onSuccess: LoginSuccessCallback) {
    this.onSuccess = onSuccess;
    this.container = document.createElement('div');
    this.container.id = 'login-overlay';
    this.buildDOM();
    document.body.appendChild(this.container);
  }

  private buildDOM(): void {
    this.container.innerHTML = '';
    Object.assign(this.container.style, {
      position: 'fixed',
      top: '0',
      left: '0',
      width: '100vw',
      height: '100vh',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      zIndex: '1000',
      fontFamily: 'Arial, Helvetica, sans-serif',
    } as CSSStyleDeclaration);

    // Card
    const card = document.createElement('div');
    Object.assign(card.style, {
      background: '#1a1a2e',
      border: '2px solid #555588',
      borderRadius: '12px',
      padding: '32px 40px',
      minWidth: '340px',
      boxShadow: '0 8px 32px rgba(0,0,0,0.6)',
      textAlign: 'center',
    } as CSSStyleDeclaration);

    // Game title
    const logo = document.createElement('h1');
    logo.textContent = 'VALHALLA';
    Object.assign(logo.style, {
      color: '#ffcc00',
      fontSize: '36px',
      margin: '0 0 4px 0',
      letterSpacing: '4px',
      textShadow: '2px 2px 4px #000',
    } as CSSStyleDeclaration);
    card.appendChild(logo);

    // Form title
    this.titleEl = document.createElement('h2');
    this.titleEl.textContent = 'Login';
    Object.assign(this.titleEl.style, {
      color: '#cccccc',
      fontSize: '18px',
      margin: '0 0 20px 0',
      fontWeight: 'normal',
    } as CSSStyleDeclaration);
    card.appendChild(this.titleEl);

    // Error message
    this.errorDiv = document.createElement('div');
    Object.assign(this.errorDiv.style, {
      color: '#ff4444',
      fontSize: '13px',
      marginBottom: '12px',
      minHeight: '18px',
    } as CSSStyleDeclaration);
    card.appendChild(this.errorDiv);

    // Username
    this.usernameInput = this.createInput('Username', 'text');
    card.appendChild(this.wrapInput(this.usernameInput));

    // Password
    this.passwordInput = this.createInput('Password', 'password');
    card.appendChild(this.wrapInput(this.passwordInput));

    // Confirm Password (register only)
    this.confirmInput = this.createInput('Confirm Password', 'password');
    this.confirmRow = this.wrapInput(this.confirmInput);
    this.confirmRow.style.display = 'none';
    card.appendChild(this.confirmRow);

    // Submit button
    this.submitBtn = document.createElement('button');
    this.submitBtn.textContent = 'Login';
    Object.assign(this.submitBtn.style, {
      width: '100%',
      padding: '12px',
      fontSize: '16px',
      fontWeight: 'bold',
      backgroundColor: '#ffcc00',
      color: '#1a1a2e',
      border: 'none',
      borderRadius: '6px',
      cursor: 'pointer',
      marginTop: '8px',
      transition: 'background 0.2s',
    } as CSSStyleDeclaration);
    this.submitBtn.addEventListener('mouseenter', () => {
      this.submitBtn.style.backgroundColor = '#ffd633';
    });
    this.submitBtn.addEventListener('mouseleave', () => {
      this.submitBtn.style.backgroundColor = '#ffcc00';
    });
    this.submitBtn.addEventListener('click', () => this.handleSubmit());
    card.appendChild(this.submitBtn);

    // Toggle link
    const toggleDiv = document.createElement('div');
    toggleDiv.style.marginTop = '16px';
    this.toggleLink = document.createElement('a');
    this.toggleLink.textContent = "Don't have an account? Register";
    Object.assign(this.toggleLink.style, {
      color: '#8888cc',
      fontSize: '13px',
      cursor: 'pointer',
      textDecoration: 'underline',
    } as CSSStyleDeclaration);
    this.toggleLink.addEventListener('click', () => this.toggleMode());
    toggleDiv.appendChild(this.toggleLink);
    card.appendChild(toggleDiv);

    this.container.appendChild(card);

    // Enter key submits
    const handleEnter = (e: KeyboardEvent) => {
      if (e.key === 'Enter') this.handleSubmit();
    };
    this.usernameInput.addEventListener('keydown', handleEnter);
    this.passwordInput.addEventListener('keydown', handleEnter);
    this.confirmInput.addEventListener('keydown', handleEnter);

    // Focus username on creation
    setTimeout(() => this.usernameInput.focus(), 100);
  }

  private createInput(placeholder: string, type: string): HTMLInputElement {
    const input = document.createElement('input');
    input.type = type;
    input.placeholder = placeholder;
    Object.assign(input.style, {
      width: '100%',
      padding: '10px 12px',
      fontSize: '14px',
      backgroundColor: '#0d0d1a',
      color: '#ffffff',
      border: '1px solid #444477',
      borderRadius: '6px',
      outline: 'none',
      boxSizing: 'border-box',
    } as CSSStyleDeclaration);
    input.addEventListener('focus', () => {
      input.style.borderColor = '#ffcc00';
    });
    input.addEventListener('blur', () => {
      input.style.borderColor = '#444477';
    });
    return input;
  }

  private wrapInput(input: HTMLInputElement): HTMLDivElement {
    const wrapper = document.createElement('div');
    wrapper.style.marginBottom = '12px';
    wrapper.appendChild(input);
    return wrapper;
  }

  private toggleMode(): void {
    this.mode = this.mode === 'login' ? 'register' : 'login';
    this.errorDiv.textContent = '';

    if (this.mode === 'register') {
      this.titleEl.textContent = 'Create Account';
      this.submitBtn.textContent = 'Register';
      this.confirmRow.style.display = 'block';
      this.toggleLink.textContent = 'Already have an account? Login';
    } else {
      this.titleEl.textContent = 'Login';
      this.submitBtn.textContent = 'Login';
      this.confirmRow.style.display = 'none';
      this.toggleLink.textContent = "Don't have an account? Register";
    }
  }

  private async handleSubmit(): Promise<void> {
    const username = this.usernameInput.value.trim();
    const password = this.passwordInput.value;

    if (!username || !password) {
      this.errorDiv.textContent = 'Please fill in all fields.';
      return;
    }

    if (this.mode === 'register') {
      const confirm = this.confirmInput.value;
      if (password !== confirm) {
        this.errorDiv.textContent = 'Passwords do not match.';
        return;
      }
    }

    // Disable button while loading
    this.submitBtn.disabled = true;
    this.submitBtn.textContent = 'Loading...';
    this.errorDiv.textContent = '';

    try {
      let result: AuthResponse;
      if (this.mode === 'register') {
        result = await AuthClient.register(username, password);
      } else {
        result = await AuthClient.login(username, password);
      }

      this.onSuccess(result);
    } catch (err: any) {
      this.errorDiv.textContent = err.message || 'Something went wrong.';
      this.submitBtn.disabled = false;
      this.submitBtn.textContent = this.mode === 'login' ? 'Login' : 'Register';
    }
  }

  /** Remove the overlay from the DOM. */
  destroy(): void {
    if (this.container.parentNode) {
      this.container.parentNode.removeChild(this.container);
    }
  }
}
